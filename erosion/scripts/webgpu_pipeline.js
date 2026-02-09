// webgpu_pipeline.js
// Main WebGPU compute pipeline orchestrator for erosion texture generation

import * as utils from './webgpu_utils.js';

/**
 * ErosionPipeline
 *
 * Orchestrates the complete WebGPU compute pipeline for generating erosion textures
 * from video frames using envelope detection and smart blur.
 */
export class ErosionPipeline {
    constructor() {
        this.device = null;
        this.adapter = null;
        this.pipelines = {};
        this.buffers = {};
        this.textures = {};
        this.bindGroups = {};
        this.width = 0;
        this.height = 0;
        this.numFrames = 0;
    }

    /**
     * Initialize WebGPU and load shaders
     */
    async initialize() {
        console.log('Initializing WebGPU pipeline...');

        // Initialize WebGPU
        const { device, adapter } = await utils.initWebGPU();
        this.device = device;
        this.adapter = adapter;

        // Load all shaders
        console.log('Loading shaders...');
        const [envelopeShader, normalizeShader, smartBlurShader, builderShader] = await Promise.all([
            utils.loadShader('shaders/envelope_detection.wgsl'),
            utils.loadShader('shaders/normalize_envelopes.wgsl'),
            utils.loadShader('shaders/smart_blur.wgsl'),
            utils.loadShader('shaders/erosion_texture_builder.wgsl')
        ]);

        // Create pipelines
        this.pipelines = {
            envelopeDetection: utils.createComputePipeline(device, envelopeShader, 'main', 'envelope_detection'),
            normalize: utils.createComputePipeline(device, normalizeShader, 'main', 'normalize'),
            smartBlur: utils.createComputePipeline(device, smartBlurShader, 'main', 'smart_blur'),
            buildErosion: utils.createComputePipeline(device, builderShader, 'main', 'erosion_builder')
        };

        console.log('All shaders compiled successfully');
    }

    /**
     * Stage 1: Load video frames into GPU texture array
     */
    async loadVideoFrames(videoElement, numFrames, width, height) {
        console.log('Stage 1: Loading video frames to GPU...');

        const frameTexture = createTexture2DArray(
            this.device,
            width,
            height,
            numFrames,
            'rgba8unorm',
            GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST,
            'video_frames'
        );

        // Extract frames using HTML5 video API
        const canvas = new OffscreenCanvas(width, height);
        const ctx = canvas.getContext('2d', { willReadFrequently: true });

        const video = document.createElement('video');
        // ... video loading logic would go here

        return frameTexture;
    }

    /**
     * Stage 2: Envelope Detection
     */
    async runEnvelopeDetection(frameTexture, width, height, numFrames, chromaKey) {
        console.log('Stage 2: Running envelope detection...');

        // Create envelope data buffer
        const envelopeBufferSize = calculateEnvelopeBufferSize(width, height);
        const envelopeBuffer = createStorageBuffer(
            this.device,
            envelopeBufferSize,
            GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC,
            'envelope_data'
        );

        // Create uniforms
        const uniformData = new Uint32Array([
            width, height, numFrames, 0, // padding for alignment
        ]);
        const chromakeyData = new Float32Array([
            chromaKey.r / 255, chromaKey.g / 255, chromaKey.b / 255, chromaKey.a / 255
        ]);

        // Combine into one uniform buffer
        const uniformData = new ArrayBuffer(32); // 16 bytes for Uniforms + 16 for chroma_key
        new Uint32Array(uniformData, 0, 3).set([width, height, numFrames]);
        new Float32Array(uniformData, 16, 4).set(chromaKeyNormalized);

        const uniformBuffer = createUniformBuffer(device, uniformData, 'envelope_uniforms');

        // Create bind group
        const bindGroup = device.createBindGroup({
            layout: pipeline.getBindGroupLayout(0),
            entries: [
                { binding: 0, resource: frameTexture.createView({ dimension: '2d-array' }) },
                { binding: 1, resource: { buffer: envelopeBuffer } },
                { binding: 2, resource: { buffer: uniformBuffer } }
            ]
        });

        // Dispatch
        const commandEncoder = device.createCommandEncoder();
        const passEncoder = commandEncoder.beginComputePass();
        passEncoder.setPipeline(pipeline);
        passEncoder.setBindGroup(0, bindGroup);
        dispatchCompute(passEncoder, pipeline, width, height);
        passEncoder.end();
        device.queue.submit([commandEncoder.finish()]);

        return { envelopeBuffer, uniformBuffer };
    }

    /**
     * Stage 3: Compute global bounds (CPU reduction)
     */
    async computeGlobalBounds(envelopeBuffer, width, height) {
        console.log('[Pipeline] Stage 3: Computing global bounds');

        const size = calculateEnvelopeBufferSize(width, height);
        const arrayBuffer = await readBuffer(this.device, envelopeBuffer, size);
        const envelopes = parseEnvelopeData(arrayBuffer, width, height);
        const bounds = computeBounds(envelopes);

        console.log('[Pipeline] Bounds:', bounds);
        return { bounds, envelopes };
    }

    /**
     * Stage 4: Normalize envelopes
     */
    async normalizeEnvelopes(envelopeBuffer, bounds, width, height) {
        console.log('[Pipeline] Stage 4: Normalizing envelopes');

        const device = this.device;
        const pipeline = this.pipelines.normalize;

        // Create output textures
        const normalizedTex = createTexture2D(
            device, width, height,
            'rgba8unorm',
            GPUTextureUsage.STORAGE_BINDING | GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_SRC,
            'normalized_texture'
        );

        const constraintsTex = createTexture2D(
            device, width, height,
            'rgba32float',
            GPUTextureUsage.STORAGE_BINDING | GPUTextureUsage.TEXTURE_BINDING,
            'constraints_texture'
        );

        // Create uniform buffers
        const boundsBuffer = createBoundsBuffer(device, bounds);
        const uniformData = new Uint32Array([width, height]);
        const uniformBuffer = createUniformBuffer(device, uniformData.buffer, 'normalize_uniforms');

        // Create bind group
        const bindGroup = device.createBindGroup({
            layout: pipeline.getBindGroupLayout(0),
            entries: [
                { binding: 0, resource: { buffer: envelopeBuffer } },
                { binding: 1, resource: normalizedTex.createView() },
                { binding: 2, resource: constraintsTex.createView() },
                { binding: 3, resource: { buffer: boundsBuffer } },
                { binding: 4, resource: { buffer: uniformBuffer } }
            ]
        });

        // Dispatch
        const commandEncoder = device.createCommandEncoder();
        const passEncoder = commandEncoder.beginComputePass();
        passEncoder.setPipeline(pipeline);
        passEncoder.setBindGroup(0, bindGroup);
        dispatchCompute(passEncoder, pipeline, width, height);
        passEncoder.end();
        device.queue.submit([commandEncoder.finish()]);

        return { normalizedTex, constraintsTex, boundsBuffer, uniformBuffer };
    }

    /**
     * Stage 5: Smart blur (iterative)
     */
    async smartBlur(normalizedTex, constraintsTex, width, height, maxIterations = 200) {
        console.log('[Pipeline] Stage 5: Smart blur (iterative)');

        const device = this.device;
        const pipeline = this.pipelines.smartBlur;

        // Create ping-pong textures
        const tempTex = createTexture2D(
            device, width, height,
            'rgba8unorm',
            GPUTextureUsage.STORAGE_BINDING | GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_SRC,
            'temp_blur_texture'
        );

        // Uniform buffer
        const uniformData = new Float32Array([
            width,
            height,
            0.01, // error_bar (will be computed per-pixel in shader)
            0.01  // convergence_threshold
        ]);
        const uniformBuffer = createUniformBuffer(device, uniformData.buffer, 'blur_uniforms');

        // Ping-pong iteration
        let inputTex = normalizedTex;
        let outputTex = tempTex;
        let previousData = null;

        for (let iter = 0; iter < maxIterations; iter++) {
            // Create bind group for this iteration
            const bindGroup = device.createBindGroup({
                layout: pipeline.getBindGroupLayout(0),
                entries: [
                    { binding: 0, resource: inputTex.createView() },
                    { binding: 1, resource: outputTex.createView() },
                    { binding: 2, resource: constraintsTex.createView() },
                    { binding: 3, resource: { buffer: uniformBuffer } }
                ]
            });

            // Dispatch blur pass
            const commandEncoder = device.createCommandEncoder();
            const passEncoder = commandEncoder.beginComputePass();
            passEncoder.setPipeline(pipeline);
            passEncoder.setBindGroup(0, bindGroup);
            dispatchCompute(passEncoder, pipeline, width, height);
            passEncoder.end();
            device.queue.submit([commandEncoder.finish()]);

            // Check convergence every 10 iterations
            if (iter % 10 === 9) {
                const { converged, maxDiff, data } = await checkConvergence(device, outputTex, previousData);
                console.log(`[Pipeline] Blur iteration ${iter + 1}: maxDiff=${maxDiff.toFixed(4)}`);

                if (converged && previousData !== null) {
                    console.log(`[Pipeline] Smart blur converged after ${iter + 1} iterations`);
                    break;
                }

                previousData = data;
            }

            // Swap ping-pong textures
            [inputTex, outputTex] = [outputTex, inputTex];
        }

        // The final result is in inputTex (after the last swap)
        return inputTex;
    }

    /**
     * Stage 6: Build erosion texture
     */
    async buildErosionTexture(envelopeBuffer, normalizedTex, alphaData, bounds, width, height, totalFrames) {
        console.log('[Pipeline] Stage 6: Building erosion texture');

        const device = this.device;
        const pipeline = this.pipelines.erosionBuilder;

        // Create alpha data storage buffer
        const alphaBuffer = createStorageBuffer(
            device,
            width * height * 16, // vec4<u32> = 16 bytes
            GPUBufferUsage.STORAGE,
            'alpha_data_buffer'
        );

        // TODO: Need to extract alpha values from envelopes and upload
        // For now, we'll need to read back envelope data and extract the alpha values
        // This is a simplification - in production you'd want to do this in the envelope detection stage

        // Create output texture
        const erosionTex = createTexture2D(
            device, width, height,
            'rgba8unorm',
            GPUTextureUsage.STORAGE_BINDING | GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_SRC | GPUTextureUsage.RENDER_ATTACHMENT,
            'erosion_texture'
        );

        // Create metadata uniform
        const attackDuration = bounds.attack_end_max - bounds.attack_start_min;
        const releaseDuration = bounds.release_end_max - bounds.release_start_min;
        const metadataData = new Int32Array([totalFrames, attackDuration, releaseDuration, 0]);
        const metadataBuffer = createUniformBuffer(device, metadataData.buffer, 'metadata_uniform');

        const uniformData = new Uint32Array([width, height]);
        const uniformBuffer = createUniformBuffer(device, uniformData.buffer, 'builder_uniforms');

        // Create bind groups
        const bindGroup0 = device.createBindGroup({
            layout: pipeline.getBindGroupLayout(0),
            entries: [
                { binding: 0, resource: { buffer: envelopeBuffer } },
                { binding: 1, resource: normalizedTex.createView() },
                { binding: 2, resource: { buffer: alphaBuffer } },
                { binding: 3, resource: erosionTex.createView() },
                { binding: 4, resource: { buffer: metadataBuffer } }
            ]
        });

        const bindGroup1 = device.createBindGroup({
            layout: pipeline.getBindGroupLayout(1),
            entries: [
                { binding: 0, resource: { buffer: uniformBuffer } }
            ]
        });

        // Dispatch
        const commandEncoder = device.createCommandEncoder();
        const passEncoder = commandEncoder.beginComputePass();
        passEncoder.setPipeline(pipeline);
        passEncoder.setBindGroup(0, bindGroup0);
        passEncoder.setBindGroup(1, bindGroup1);
        dispatchCompute(passEncoder, pipeline, width, height);
        passEncoder.end();
        device.queue.submit([commandEncoder.finish()]);

        return erosionTex;
    }

    /**
     * Main pipeline execution
     */
    async processVideo(frames, chromaKey = { r: 0, g: 0, b: 0, a: 0 }, progressCallback = null) {
        console.log(`[Pipeline] Processing ${frames.length} frames`);

        const width = frames[0].width;
        const height = frames[0].height;
        const numFrames = frames.length;

        try {
            // Stage 1: Upload frames
            if (progressCallback) progressCallback({ stage: 'upload', progress: 0 });
            const frameTexture = await this.uploadFrames(frames);
            if (progressCallback) progressCallback({ stage: 'upload', progress: 1 });

            // Stage 2: Envelope detection
            if (progressCallback) progressCallback({ stage: 'envelope_detection', progress: 0 });
            const { envelopeBuffer } = await this.detectEnvelopes(frameTexture, chromaKey, width, height, numFrames);
            if (progressCallback) progressCallback({ stage: 'envelope_detection', progress: 1 });

            // Stage 3: Compute bounds
            if (progressCallback) progressCallback({ stage: 'compute_bounds', progress: 0 });
            const { bounds, envelopes } = await this.computeGlobalBounds(envelopeBuffer, width, height);
            if (progressCallback) progressCallback({ stage: 'compute_bounds', progress: 1 });

            // Stage 4: Normalize
            if (progressCallback) progressCallback({ stage: 'normalize', progress: 0 });
            const { normalizedTex, constraintsTex } = await this.normalizeEnvelopes(envelopeBuffer, bounds, width, height);
            if (progressCallback) progressCallback({ stage: 'normalize', progress: 1 });

            // Stage 5: Smart blur (optional - check if needed)
            let finalNormalizedTex = normalizedTex;
            const attackDuration = bounds.attack_end_max - bounds.attack_start_min;
            const needsBlur = attackDuration <= 255;

            if (needsBlur) {
                if (progressCallback) progressCallback({ stage: 'smart_blur', progress: 0 });
                finalNormalizedTex = await this.smartBlur(normalizedTex, constraintsTex, width, height);
                if (progressCallback) progressCallback({ stage: 'smart_blur', progress: 1 });
            } else {
                console.log('[Pipeline] Skipping smart blur (high frame rate)');
            }

            // Stage 6: Build erosion texture
            if (progressCallback) progressCallback({ stage: 'erosion_texture', progress: 0 });
            const erosionTex = await this.buildErosionTexture(
                envelopeBuffer,
                finalNormalizedTex,
                envelopes, // TODO: extract alpha data
                bounds,
                width,
                height,
                numFrames
            );
            if (progressCallback) progressCallback({ stage: 'erosion_texture', progress: 1 });

            console.log('[Pipeline] Processing complete!');

            return {
                erosionTexture: erosionTex,
                metadata: {
                    width,
                    height,
                    totalFrames: numFrames,
                    fadeInDuration: (bounds.attack_end_max - bounds.attack_start_min) / numFrames,
                    fadeOutDuration: (bounds.release_end_max - bounds.release_start_min) / numFrames,
                    bounds
                }
            };

        } catch (error) {
            console.error('[Pipeline] Error:', error);
            throw error;
        }
    }

    /**
     * Cleanup resources
     */
    destroy() {
        // WebGPU will handle cleanup when device is destroyed
        // In a production app, you'd want to explicitly destroy buffers/textures
        console.log('[Pipeline] Cleanup complete');
    }
}
