// prototype/worker.js
// Bridge between main thread and WASM effect processing module.
// See docs/plans/2026-01-24-effect-stack-architecture.md for full design.

'use strict';

importScripts('https://cdn.jsdelivr.net/npm/fflate@0.8.2/umd/index.min.js');

// Will be set after WASM module loads
let Module = null;
let cwrapped = null;

// --- WASM function bindings (set in initCFunctions) ---

function initCFunctions() {
    const cwrap = Module.cwrap;

    cwrapped = {
        // Catalog generation
        init_catalog:        cwrap('init_catalog', null, []),

        // Source management
        set_source_path:     cwrap('set_source_path', null, ['number', 'string']),
        set_source_changed:  cwrap('set_source_changed', null, ['number', 'number']),

        // Stack execution
        stack_begin:         cwrap('stack_begin', null, ['number']),
        push_effect:         cwrap('push_effect', null, ['number', 'number', 'number']),
        stack_end:           cwrap('stack_end', 'number', ['number', 'number']),

        // Analysis
        analyze_source:      cwrap('analyze_source', null, ['number', 'number', 'number']),
    };
}

// --- WASM callback implementations ---
// These are passed to createEffectStack() and called by js_imports.js
// when C code invokes the corresponding extern functions.

const wasmCallbacks = {
    onError: (errorCode, effectId, paramIdx, message) => {
        self.postMessage({
            type: 'wasmError',
            errorCode,
            effectId,
            paramIdx,
            message
        });
    },

    onClearAutoEffects: (stackType) => {
        self.postMessage({ type: 'clearAutoEffects', stackType });
    },

    onPushAutoEffect: (stackType, effectId, params) => {
        self.postMessage({
            type: 'pushAutoEffect',
            stackType,
            effectId,
            params
        });
    },

    onSetSourceTiming: (stackType, fadeInTime, fadeOutTime, totalDuration) => {
        self.postMessage({
            type: 'setSourceTiming',
            stackType,
            fadeInTime,
            fadeOutTime,
            totalDuration
        });
    }
};

// --- Message handlers ---

// Cached catalog (loaded at init)
let effectCatalog = null;

// --- Request coalescing ---
// Store the latest pending request per type+stackType to avoid queue buildup
// Key format: "type:stackType" (e.g., "updateSourceParams:1")
let pendingRequests = {};
let isProcessing = {};  // Track if we're currently processing a given key

async function handleInit(msg) {
    // Fetch the static catalog JSON
    const response = await fetch('scripts/effect_catalog.json');
    if (!response.ok) {
        throw new Error('Failed to load effect catalog: ' + response.status);
    }
    effectCatalog = await response.json();

    // Transform catalog for main thread (expects { gradient, erosion } with standalone + groups)
    const catalog = {
        gradient: {
            standalone: effectCatalog.stacks.gradient.standalone || [],
            groups: effectCatalog.stacks.gradient.groups || []
        },
        erosion: {
            standalone: effectCatalog.stacks.erosion.standalone || [],
            groups: effectCatalog.stacks.erosion.groups || []
        }
    };

    return { id: msg.id, type: 'ready', catalog };
}

function handleLoadSource(msg) {
    const { stackType, fileName, fileData, quantization = 1.0 } = msg;
    const vfsDir = '/sources';
    const vfsPath = vfsDir + '/' + fileName;

    // Ensure directory exists
    try { Module.FS.mkdir(vfsDir); } catch (e) { /* already exists */ }

    // Write file to VFS
    Module.FS.writeFile(vfsPath, new Uint8Array(fileData));

    // Tell WASM about the new source
    cwrapped.set_source_path(stackType, vfsPath);
    cwrapped.set_source_changed(stackType, 1);

    // Analyze source with quantization param
    // params[0] = quantization (0.0 = 1 bit, 1.0 = 8 bits)
    const paramsPtr = Module._malloc(4);
    Module.HEAPF32[paramsPtr >> 2] = quantization;
    cwrapped.analyze_source(stackType, paramsPtr, 1);
    Module._free(paramsPtr);

    return { id: msg.id, type: 'sourceLoaded', stackType };
}

function handleUpdateSourceParams(msg) {
    const { stackType, quantization } = msg;

    // Re-analyze with new quantization (triggers reload if changed)
    const paramsPtr = Module._malloc(4);
    Module.HEAPF32[paramsPtr >> 2] = quantization;
    cwrapped.analyze_source(stackType, paramsPtr, 1);
    Module._free(paramsPtr);

    return { id: msg.id, type: 'sourceParamsUpdated', stackType };
}

function handleProcessStack(msg) {
    const { stackType, effects } = msg;

    cwrapped.stack_begin(stackType);

    // Push each effect's params into WASM (params are now Uint8Array)
    for (const effect of effects) {
        const params = effect.params;  // Uint8Array
        const paramCount = params.length;

        // Allocate WASM heap space for the uint8 buffer
        const ptr = Module._malloc(paramCount);
        Module.HEAPU8.set(params, ptr);

        cwrapped.push_effect(effect.effectId, ptr, paramCount);

        Module._free(ptr);
    }

    // Finish processing — get output dimensions and image pointer
    const outWPtr = Module._malloc(4);
    const outHPtr = Module._malloc(4);

    const imagePtr = cwrapped.stack_end(outWPtr, outHPtr);

    const width = Module.getValue(outWPtr, 'i32');
    const height = Module.getValue(outHPtr, 'i32');

    Module._free(outWPtr);
    Module._free(outHPtr);

    // Handle stub/empty result (WASM not fully implemented yet)
    if (!imagePtr || width <= 0 || height <= 0) {
        return {
            response: { id: msg.id, type: 'stackResult', stackType, width: 0, height: 0, imageData: new Uint8Array(0) },
            transfer: []
        };
    }

    // Copy image data out of WASM heap
    const byteLength = width * height * 4;
    const imageData = new Uint8Array(Module.HEAPU8.buffer, imagePtr, byteLength).slice();

    // Collect debug files from VFS
    const debugFiles = collectDebugFiles();

    // Transfer the buffer to main thread (zero-copy)
    const transfer = [imageData.buffer];
    if (debugFiles) {
        transfer.push(debugFiles.data.buffer);
    }

    return {
        response: { id: msg.id, type: 'stackResult', stackType, width, height, imageData, debugFiles },
        transfer
    };
}

// --- Debug file collection ---

// Collect all debug files from WASM virtual filesystem
// Returns { data, filename } or null
function collectDebugFiles() {
    const skipDirs = new Set(['dev', 'tmp', 'home', 'proc', 'sources', '.', '..']);
    const files = {};

    try {
        const entries = Module.FS.readdir('/');

        for (const name of entries) {
            if (skipDirs.has(name)) continue;

            try {
                const stat = Module.FS.stat('/' + name);
                if (Module.FS.isFile(stat.mode)) {
                    const data = Module.FS.readFile('/' + name);
                    files[name] = data;
                    Module.FS.unlink('/' + name);  // cleanup
                    console.log(`Collected debug file: ${name} (${data.length} bytes)`);
                }
            } catch (e) {
                // Not a file or can't read, skip
            }
        }
    } catch (e) {
        console.log('Error enumerating VFS:', e);
        return null;
    }

    const fileNames = Object.keys(files);
    if (fileNames.length === 0) {
        return null;
    }

    // If only one file, return it directly
    if (fileNames.length === 1) {
        const filename = fileNames[0];
        console.log(`Returning single debug file: ${filename}`);
        return { data: files[filename], filename };
    }

    console.log(`Zipping ${fileNames.length} debug files...`);

    // Use fflate to create zip
    const zipData = fflate.zipSync(files);
    console.log(`Created debug.zip (${zipData.length} bytes)`);

    return { data: zipData, filename: 'debug.zip' };
}

// --- Module initialization ---

importScripts('scripts/effect_stack.js');

createEffectStack({
    // Tell Emscripten where to find the WASM file (relative to worker, not script)
    locateFile: (path) => 'scripts/' + path,
    // Wire up callbacks for C->JS communication
    ...wasmCallbacks
}).then(mod => {
    Module = mod;
    initCFunctions();
    self.postMessage({ type: 'init', success: true });
}).catch(err => {
    self.postMessage({ type: 'init', success: false, error: err.message });
});

// --- Message dispatch ---

// Helper to create a coalescing key for requests that should drop stale messages
function getCoalesceKey(msg) {
    // Only coalesce certain message types
    if (msg.type === 'updateSourceParams' || msg.type === 'processStack') {
        return msg.type + ':' + msg.stackType;
    }
    return null;  // Don't coalesce other types
}

// Process a single message
async function processMessage(msg) {
    switch (msg.type) {
    case 'init': {
        const response = await handleInit(msg);
        self.postMessage(response);
        break;
    }
    case 'loadSource': {
        const response = handleLoadSource(msg);
        self.postMessage(response);
        break;
    }
    case 'processStack': {
        const result = handleProcessStack(msg);
        self.postMessage(result.response, result.transfer);
        break;
    }
    case 'updateSourceParams': {
        const response = handleUpdateSourceParams(msg);
        self.postMessage(response);
        break;
    }
    default:
        self.postMessage({
            id: msg.id,
            type: 'error',
            error: 'Unknown message type: ' + msg.type
        });
    }
}

// Check and process any pending requests after a delay
async function processPendingAfterDelay(key) {
    // Wait 10ms before checking for pending requests
    await new Promise(resolve => setTimeout(resolve, 10));

    // Check if there's a pending request for this key
    const pending = pendingRequests[key];
    if (pending) {
        delete pendingRequests[key];
        try {
            await processMessage(pending);
        } catch (err) {
            self.postMessage({
                id: pending.id,
                type: 'error',
                error: err.message || String(err)
            });
        }
        // Recursively check for more pending requests
        await processPendingAfterDelay(key);
    } else {
        // No more pending requests, mark as not processing
        isProcessing[key] = false;
    }
}

self.onmessage = async function(e) {
    const msg = e.data;
    const key = getCoalesceKey(msg);

    // For coalesced message types, check if we're already processing
    if (key) {
        if (isProcessing[key]) {
            // Store as pending (replaces any previous pending request)
            pendingRequests[key] = msg;
            return;
        }
        // Mark as processing
        isProcessing[key] = true;
    }

    try {
        await processMessage(msg);
    } catch (err) {
        self.postMessage({
            id: msg.id,
            type: 'error',
            error: err.message || String(err)
        });
    }

    // For coalesced types, check for pending requests after delay
    if (key) {
        await processPendingAfterDelay(key);
    }
};
