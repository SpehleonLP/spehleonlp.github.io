// webgpu_video_processor.js
// Integration layer between HTML5 video loading and WebGPU pipeline

import * as utils from './webgpu_utils.js';

/**
 * Process video using WebGPU compute pipeline
 * This replaces the old WASM worker approach
 */
export class WebGPUVideoProcessor {
    constructor() {
        this.device = null;
        this.pipelines = {};
        this.initialized = false;
    }

    /**
     * Initialize WebGPU and compile shaders
     */
    async init() {
        if (this.initialized) return;

        console.log('[WebGPU] Initializing...');
        const { device } = await utils.initWebGPU();
        this.device = device;

        // Load and compile all shaders
        const shaderFiles = [
            'shaders/envelope_detection.wgsl',
            'shaders/normalize_envelopes.wgsl',
            'shaders/smart_blur.wgsl',
            'shaders/erosion_texture_builder.wgsl'
        ];

        const shaderCodes = await Promise.all(shaderFiles.map(utils.loadShader));

        this.pipelines = {
            envelopeDetection: utils.createComputePipeline(this.device, shaderCodes[0], 'main', 'envelope'),
            normalize: utils.createComputePipeline(this.device, shaderCodes[1], 'main', 'normalize'),
            smartBlur: utils.createComputePipeline(this.device, shaderCodes[2], 'main', 'blur'),
            buildErosion: utils.createComputePipeline(this.device, shaderCodes[3], 'main', 'builder')
        };

        this.initialized = true;
        console.log('[WebGPU] Initialization complete');
    }

    /**
     * Extract frames from video element
     * This reuses the existing HTML5 Video API approach
     */
    async extractFrames(videoElement, estimatedFPS = 30) {
        const duration = videoElement.duration;
        const frameInterval = 1 / estimatedFPS;
        const totalFrames = Math.floor(duration * estimatedFPS);

        console.log(`[WebGPU] Extracting ${totalFrames} frames...`);

        const canvas = new OffscreenCanvas(videoElement.videoWidth, videoElement.videoHeight);
        const ctx = canvas.getContext('2d', { willReadFrequently: true });

        const frames = [];

        for (let frame = 0; frame < totalFrames; frame++) {
            const time = frame * frameInterval;
            if (time > duration) break;

            // Seek to frame
            videoElement.currentTime = time;
            await new Promise(resolve => {
                videoElement.onseeked = resolve;
            });

            // Draw and capture
            ctx.clearRect(0, 0, canvas.width, canvas.height);
            ctx.drawImage(videoElement, 0, 0);
            const imageData = ctx.getImageData(0, 0, canvas.width, canvas.height);

            frames.push(imageData);
        }

        console.log(`[WebGPU] Extracted ${frames.length} frames`);
        return frames;
    }

    /**
     * Upload frames to GPU as 2D texture array
     */
    uploadFramesToGPU(frames) {
        const width = frames[0].width;
        const height = frames[0].height;
        const numFrames = frames.length;

        console.log(`[WebGPU] Uploading ${numFrames} frames to GPU...`);

        const texture = utils.createTexture2DArray(
            this.device,
            width,
            height,
            numFrames,
            'rgba8unorm',
            GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST,
            'video_frames'
        );

        // Upload each frame
        for (let i = 0; i < numFrames; i++) {
            utils.uploadFrameToTextureArray(this.device, texture, i, frames[i]);
        }

        return texture;
    }

    /**
     * Run envelope detection compute shader
     */
    async detectEnvelopes(frameTexture, width, height, numFrames, chromaKey) {
        console.log('[WebGPU] Running envelope detection...');

        const device = this.device;
        const pipeline = this.pipelines.envelopeDetection;

        // Create storage buffer for envelope data
        const envelopeBufferSize = utils.calculateEnvelopeBufferSize(width, height);
        const envelopeBuffer = utils.createStorageBuffer(
            device,
            envelopeBufferSize,
            GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC,
            'envelopes'
        );

        // Create uniform buffer (Uniforms struct)
        const uniformsData = new Uint32Array([width, height, numFrames, 0]);
        const chromaData = new Float32Array([
            chromaKey.r / 255,
            chromaKey.g / 255,
            chromaKey.b / 255,
            chromaKey.a / 255
        ]);

        // Pack into one buffer: 16 bytes uniforms + 16 bytes chroma
        const uniformBuffer = device.createBuffer({
            size: 32,
            usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST
        });
        device.queue.writeBuffer(uniformBuffer, 0, uniformsData);
        device.queue.writeBuffer(uniformBuffer, 16, chromaData);

        // Bind group
        const bindGroup = utils.createBindGroup(device, pipeline, 0, [
            { binding: 0, resource: frameTexture.createView({ dimension: '2d-array' }) },
            { binding: 1, resource: { buffer: envelopeBuffer } },
            { binding: 2, resource: { buffer: uniformBuffer } }
        ], 'envelope_bind_group');

        // Execute
        const commandEncoder = device.createCommandEncoder();
        const pass = commandEncoder.beginComputePass();
        pass.setBindGroup(0, bindGroup);
        utils.dispatchCompute(pass, pipeline, width, height);
        pass.end();
        device.queue.submit([commandEncoder.finish()]);

        // Wait for completion
        await device.queue.onSubmittedWorkDone();

        return envelopeBuffer;
    }

    /**
     * Process video and generate erosion texture
     * Main entry point
     */
    async processVideo(videoElement, progressCallback = null) {
        await this.init();

        try {
            // Extract frames
            if (progressCallback) progressCallback({ stage: 'extract', progress: 0, message: 'Extracting frames...' });
            const frames = await this.extractFrames(videoElement);
            const width = frames[0].width;
            const height = frames[0].height;
            const numFrames = frames.length;

            // Sample chromakey from first frame's top-left pixel
            const chromaKey = {
                r: frames[0].data[0],
                g: frames[0].data[1],
                b: frames[0].data[2],
                a: frames[0].data[3]
            };

            // Upload to GPU
            if (progressCallback) progressCallback({ stage: 'upload', progress: 0.1, message: 'Uploading to GPU...' });
            const frameTexture = this.uploadFramesToGPU(frames);

            // Stage 2: Envelope detection
            if (progressCallback) progressCallback({ stage: 'envelope', progress: 0.2, message: 'Detecting envelopes...' });
            const envelopeBuffer = await this.detectEnvelopes(frameTexture, width, height, numFrames, chromaKey);

            // Stage 3: Read back and compute bounds
            if (progressCallback) progressCallback({ stage: 'bounds', progress: 0.4, message: 'Computing bounds...' });
            const size = utils.calculateEnvelopeBufferSize(width, height);
            const arrayBuffer = await utils.readBuffer(this.device, envelopeBuffer, size);
            const envelopes = utils.parseEnvelopeData(arrayBuffer, width, height);
            const bounds = utils.computeBounds(envelopes);

            console.log('[WebGPU] Bounds computed:', bounds);

            // Stage 4: Normalize (simplified for now)
            if (progressCallback) progressCallback({ stage: 'normalize', progress: 0.5, message: 'Normalizing...' });
            // TODO: Implement normalize stage

            // Stage 5: Smart blur (simplified for now)
            if (progressCallback) progressCallback({ stage: 'blur', progress: 0.7, message: 'Smoothing...' });
            // TODO: Implement smart blur stage

            // Stage 6: Build erosion texture (simplified for now)
            if (progressCallback) progressCallback({ stage: 'build', progress: 0.9, message: 'Building erosion texture...' });
            // TODO: Implement erosion texture builder

            if (progressCallback) progressCallback({ stage: 'complete', progress: 1.0, message: 'Complete!' });

            // Return metadata for now
            return {
                width,
                height,
                numFrames,
                bounds,
                fadeInDuration: (bounds.attack_end_max - bounds.attack_start_min) / numFrames,
                fadeOutDuration: (bounds.release_end_max - bounds.release_start_min) / numFrames
            };

        } catch (error) {
            console.error('[WebGPU] Pipeline error:', error);
            throw error;
        }
    }
}

// Export singleton instance
export const videoProcessor = new WebGPUVideoProcessor();
