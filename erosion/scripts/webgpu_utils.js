// webgpu_utils.js
// Utility functions for WebGPU setup and operations

/**
 * Initialize WebGPU and return device and context
 */
export async function initWebGPU() {
    if (!navigator.gpu) {
        throw new Error('WebGPU not supported. Please use a browser that supports WebGPU.');
    }

    const adapter = await navigator.gpu.requestAdapter();
    if (!adapter) {
        throw new Error('Failed to get WebGPU adapter.');
    }

    const device = await adapter.requestDevice();
    if (!device) {
        throw new Error('Failed to get WebGPU device.');
    }

    console.log('WebGPU initialized successfully');
    return { device, adapter };
}

/**
 * Load a WGSL shader from file
 */
export async function loadShader(path) {
    const response = await fetch(path);
    if (!response.ok) {
        throw new Error(`Failed to load shader: ${path}`);
    }
    return await response.text();
}

/**
 * Create a compute pipeline from WGSL code
 */
export function createComputePipeline(device, shaderCode, entryPoint = 'main', label = 'compute') {
    const shaderModule = device.createShaderModule({
        label: `${label}_shader`,
        code: shaderCode
    });

    return device.createComputePipeline({
        label: `${label}_pipeline`,
        layout: 'auto',
        compute: {
            module: shaderModule,
            entryPoint: entryPoint
        }
    });
}

/**
 * Create a storage buffer
 */
export function createStorageBuffer(device, size, usage = GPUBufferUsage.STORAGE, label = 'storage') {
    return device.createBuffer({
        label: label,
        size: size,
        usage: usage | GPUBufferUsage.COPY_SRC | GPUBufferUsage.COPY_DST
    });
}

/**
 * Create a uniform buffer with initial data
 */
export function createUniformBuffer(device, data, label = 'uniform') {
    const buffer = device.createBuffer({
        label: label,
        size: data.byteLength,
        usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST
    });
    device.queue.writeBuffer(buffer, 0, data);
    return buffer;
}

/**
 * Read data back from a GPU buffer to CPU
 */
export async function readBuffer(device, buffer, size) {
    const readBuffer = device.createBuffer({
        size: size,
        usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ
    });

    const commandEncoder = device.createCommandEncoder();
    commandEncoder.copyBufferToBuffer(buffer, 0, readBuffer, 0, size);
    device.queue.submit([commandEncoder.finish()]);

    await readBuffer.mapAsync(GPUMapMode.READ);
    const arrayBuffer = readBuffer.getMappedRange().slice(0);
    readBuffer.unmap();
    readBuffer.destroy();

    return arrayBuffer;
}

/**
 * Create a 2D texture
 */
export function createTexture2D(device, width, height, format, usage, label = 'texture2d') {
    return device.createTexture({
        label: label,
        size: { width, height, depthOrArrayLayers: 1 },
        format: format,
        usage: usage,
        dimension: '2d'
    });
}

/**
 * Create a 2D texture array (for video frames)
 */
export function createTexture2DArray(device, width, height, numLayers, format, usage, label = 'texture2d_array') {
    return device.createTexture({
        label: label,
        size: { width, height, depthOrArrayLayers: numLayers },
        format: format,
        usage: usage,
        dimension: '2d'
    });
}

/**
 * Upload image data to a 2D texture array layer
 */
export function uploadFrameToTextureArray(device, texture, layerIndex, imageData) {
    device.queue.writeTexture(
        { texture: texture, origin: { x: 0, y: 0, z: layerIndex } },
        imageData.data,
        { bytesPerRow: imageData.width * 4, rowsPerImage: imageData.height },
        { width: imageData.width, height: imageData.height, depthOrArrayLayers: 1 }
    );
}

/**
 * Create bind group from layout
 */
export function createBindGroup(device, pipeline, groupIndex, entries, label = 'bind_group') {
    return device.createBindGroup({
        label: label,
        layout: pipeline.getBindGroupLayout(groupIndex),
        entries: entries
    });
}

/**
 * Dispatch compute shader with proper workgroup sizing
 */
export function dispatchCompute(passEncoder, pipeline, width, height, workgroupSize = 8) {
    const workgroupsX = Math.ceil(width / workgroupSize);
    const workgroupsY = Math.ceil(height / workgroupSize);

    passEncoder.setPipeline(pipeline);
    passEncoder.dispatchWorkgroups(workgroupsX, workgroupsY, 1);
}

/**
 * Calculate aligned buffer size (WebGPU requires 4-byte alignment)
 */
export function alignBufferSize(size, alignment = 4) {
    return Math.ceil(size / alignment) * alignment;
}

/**
 * Create struct-aligned buffer for envelope data
 * EnvelopeData struct is 32 bytes (8 fields × 4 bytes)
 */
export function calculateEnvelopeBufferSize(width, height) {
    const structSize = 32; // sizeof(EnvelopeData) = 8 * 4 bytes
    return width * height * structSize;
}

/**
 * Parse envelope data from GPU buffer
 */
export function parseEnvelopeData(arrayBuffer, width, height) {
    const view = new Int32Array(arrayBuffer);
    const envelopes = [];

    for (let i = 0; i < width * height; i++) {
        const offset = i * 8; // 8 i32s per envelope (with padding)
        envelopes.push({
            attack_start: view[offset + 0],
            attack_end: view[offset + 1],
            release_start: view[offset + 2],
            release_end: view[offset + 3],
            has_envelope: view[offset + 4],
        });
    }

    return envelopes;
}

/**
 * Compute global bounds from envelope data (CPU reduction)
 */
export function computeBounds(envelopes) {
    const bounds = {
        attack_start_min: Infinity,
        attack_start_max: -Infinity,
        attack_end_min: Infinity,
        attack_end_max: -Infinity,
        release_start_min: Infinity,
        release_start_max: -Infinity,
        release_end_min: Infinity,
        release_end_max: -Infinity,
    };

    for (const env of envelopes) {
        if (env.has_envelope) {
            bounds.attack_start_min = Math.min(bounds.attack_start_min, env.attack_start);
            bounds.attack_start_max = Math.max(bounds.attack_start_max, env.attack_start);
            bounds.attack_end_min = Math.min(bounds.attack_end_min, env.attack_end);
            bounds.attack_end_max = Math.max(bounds.attack_end_max, env.attack_end);
            bounds.release_start_min = Math.min(bounds.release_start_min, env.release_start);
            bounds.release_start_max = Math.max(bounds.release_start_max, env.release_start);
            bounds.release_end_min = Math.min(bounds.release_end_min, env.release_end);
            bounds.release_end_max = Math.max(bounds.release_end_max, env.release_end);
        }
    }

    // Handle case where no envelopes found
    if (!isFinite(bounds.attack_start_min)) {
        throw new Error('No valid envelopes found in video');
    }

    return bounds;
}

/**
 * Create bounds uniform buffer
 */
export function createBoundsBuffer(device, bounds) {
    const data = new Int32Array([
        bounds.attack_start_min,
        bounds.attack_start_max,
        bounds.attack_end_min,
        bounds.attack_end_max,
        bounds.release_start_min,
        bounds.release_start_max,
        bounds.release_end_min,
        bounds.release_end_max,
    ]);
    return createUniformBuffer(device, data.buffer, 'bounds_uniform');
}

/**
 * Check if texture values have converged (for smart blur)
 */
export async function checkConvergence(device, texture, previousData, threshold = 0.01) {
    // Read back texture data
    const size = texture.width * texture.height * 4; // RGBA
    const buffer = device.createBuffer({
        size: size,
        usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ
    });

    const commandEncoder = device.createCommandEncoder();
    commandEncoder.copyTextureToBuffer(
        { texture: texture },
        { buffer: buffer, bytesPerRow: texture.width * 4 },
        { width: texture.width, height: texture.height }
    );
    device.queue.submit([commandEncoder.finish()]);

    await buffer.mapAsync(GPUMapMode.READ);
    const currentData = new Uint8Array(buffer.getMappedRange());

    // Check max difference
    let maxDiff = 0;
    for (let i = 0; i < currentData.length; i++) {
        const diff = Math.abs(currentData[i] - previousData[i]) / 255.0;
        if (diff > maxDiff) {
            maxDiff = diff;
        }
    }

    buffer.unmap();
    buffer.destroy();

    return { converged: maxDiff < threshold, maxDiff: maxDiff, data: currentData };
}
