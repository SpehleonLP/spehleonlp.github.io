// worker.js

// Import the Emscripten-generated script
importScripts('video_processor.js');

// Wrapper class to manage WASM interactions
class VideoProcessorWorker {
    constructor() {
        this.module = null;
        this.initialized = false;
        this.width = 0;
        this.height = 0;

        console.log("created worker");
    }

    // Initialize the WASM module
    async init() {
        return new Promise((resolve, reject) => {
            createModule(
                {
                    onError: (err) => {
                        reject(err);
                    }
                }
            ).then(Module => {
                try {
                    // Verify that Module is defined
                    if (typeof Module === 'undefined') {
                        throw new Error("Module is undefined after runtime initialization.");
                    }

                    this.module = Module;
                    // Initialize C functions
                    this.initializeCFunctions();
                    this.initialized = true;
                    resolve();
                } catch (initError) {
                    console.log(`Initialization Error: ${initError.message}`);
                    reject(initError);
                }
            });
        });
    }

    // Wrap exported C functions using cwrap
    initializeCFunctions() {
        if (typeof this.module.cwrap !== 'function') {
            console.error("cwrap is not available. Ensure it's exported during compilation.");
            return;
        }

        if (typeof this.module._malloc !== 'function' || typeof this.module._free !== 'function') {
            console.log("module:", this.module);
            console.error("malloc or free is not available. Ensure they're exported during compilation.");
            return;
        }

        if (typeof this.module.getValue !== 'function' || typeof this.module.setValue !== 'function') {
            console.log("module:", this.module);
            console.error("getValue or setValue is not available. Ensure they're exported during compilation.");
            return;
        }


        this.initialize = this.module.cwrap('initialize', null, ['number', 'number']);
        this.push_frame = this.module.cwrap('push_frame', null, ['number']);
        this.finished_pass = this.module.cwrap('finish_pass', 'number', []);
        this.finish_processing = this.module.cwrap('finish_processing', null, []);
        this.get_image = this.module.cwrap('get_image', 'number', ['number']);
        this.shutdownAndRelease = this.module.cwrap('shutdownAndRelease', null, []);

        console.log("push_frame function bound:", !!this.push_frame);
        console.log("finished_pass function bound:", !!this.finished_pass);
        console.log("finish_processing function bound:", !!this.finish_processing);
        console.log("get_image function bound:", !!this.get_image);
        console.log("shutdownAndRelease function bound:", !!this.shutdownAndRelease);
    }

    // Initialize the C processor
    initializeProcessor(width, height) {
        this.width = width;
        this.height = height;
        this.initialize(width, height);
    }

    // Push a frame to the C processor
	pushFrame(frameDataBuffer) {
		// Ensure frameDataBuffer is an ArrayBuffer or Uint8Array
		let data;
		if (frameDataBuffer instanceof ArrayBuffer) {
		    data = new Uint8Array(frameDataBuffer);
		} else if (frameDataBuffer instanceof Uint8Array) {
		    data = frameDataBuffer;
		} else {
		    throw new Error('Expected an ArrayBuffer or Uint8Array.');
		}

		const numBytes = data.length;

		const ptr = this.module._malloc(numBytes); // Allocate memory in WebAssembly
		this.module.HEAPU8.set(data, ptr);         // Copy data into WebAssembly memory
		this.push_frame(ptr);                      // Call the C function
		this.module._free(ptr);                    // Free allocated memory
	}


    // Finish processing and retrieve images
    finishPass() {
        this.finished_pass();
    }


    // Finish processing and retrieve images
    finishProcessing() {
        this.finish_processing();

        const erosionPtr = this.get_image(0);
        const gradientPtr = this.get_image(1);

        const erosion = this.extractImageData(erosionPtr);
        const gradient = this.extractImageData(gradientPtr);

        this.shutdownAndRelease();

        return { erosion, gradient };
    }

    // Extract ImageData from a pointer
    extractImageData(ptr) {
        if (ptr === 0) return null;

        const width = this.module.getValue(ptr, 'i32');
        const height = this.module.getValue(ptr + 4, 'i32');
        const depth = this.module.getValue(ptr + 8, 'i32');
        const dataPtr = ptr + 16;
        const dataLength = depth * width * height * 4;
        const data = new Uint8ClampedArray(this.module.HEAPU8.buffer, dataPtr, dataLength).slice();

        return new ImageData(new Uint8ClampedArray(data), width, height * depth);
    }
}

// Instantiate the processor
const processor = new VideoProcessorWorker();

// Initialize the processor when the worker starts
processor.init().then(() => {
    self.postMessage({ type: 'init', success: true });
}).catch((err) => {
    self.postMessage({ type: 'init', success: false, error: err.message });
});

self.onmessage = function(e) {
    const { id, type, data } = e.data;
    try {
        switch(type) {
            case 'initialize':
                // Initialize your processor with data.width and data.height
                processor.initializeProcessor(data.width, data.height);
                self.postMessage({ id, type: 'initialized' });
                break;
            case 'pushFrame':
                // Assuming data.frameData is an ArrayBuffer
                processor.pushFrame(data.frameData);
                self.postMessage({ id, type: 'framePushed' });
                break;
            case 'finishPass':
                processor.finishPass();
                self.postMessage({ id, type: 'passFinished' });
                break;
            case 'finish':
                const results = processor.finishProcessing();
                self.postMessage({ id, type: 'finished', data: results });
                break;
            default:
                throw new Error(`Unknown message type: ${type}`);
        }
    } catch (error) {
        self.postMessage({ id, type: 'ERROR', data: error.message });
    }
}
