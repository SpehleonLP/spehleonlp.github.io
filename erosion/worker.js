// worker.js

// Import the Emscripten-generated script
importScripts('video_processor.js');

// Wrapper class to manage WASM interactions
class VideoProcessorWorker {
    constructor() {
        this.module = null;
        this.initialized = false;
        this.totalFrames = 0;
        this.width = 0;
        this.height = 0;
    }

    // Initialize the WASM module
    async init() {
        return new Promise((resolve, reject) => {
            createModule({
                onRuntimeInitialized: () => {
                    this.module = Module;
                    this.initializeCFunctions();
                    this.initialized = true;
                    resolve();
                },
                onError: (err) => {
                    reject(err);
                }
            });
        });
    }

    // Wrap exported C functions using cwrap
    initializeCFunctions() {
        this.initialize = this.module.cwrap('initialize', null, ['number', 'number', 'number']);
        this.push_frame = this.module.cwrap('push_frame', null, ['number']);
        this.finish_processing = this.module.cwrap('finish_processing', null, []);
        this.get_image = this.module.cwrap('get_image', 'number', ['number']);
        this.shutdownAndRelease = this.module.cwrap('shutdownAndRelease', null, []);
    }

    // Initialize the C processor
    initializeProcessor(totalFrames, width, height) {
        this.totalFrames = totalFrames;
        this.width = width;
        this.height = height;
        this.initialize(totalFrames, width, height);
    }

    // Push a frame to the C processor
    pushFrame(data) {
        const numBytes = data.length;
        const ptr = this.module._malloc(numBytes);
        this.module.HEAPU8.set(data, ptr);
        this.push_frame(ptr);
        this.module._free(ptr);
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
        const dataPtr = ptr + 8;
        const dataLength = width * height * 4;
        const data = new Uint8ClampedArray(this.module.HEAPU8.buffer, dataPtr, dataLength).slice();

        return new ImageData(new Uint8ClampedArray(data), width, height);
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

// Listen for messages from the main thread
self.onmessage = function(e) {
    const { type, data } = e.data;

    if (type === 'initialize') {
        const { totalFrames, width, height } = data;
        processor.initializeProcessor(totalFrames, width, height);
        self.postMessage({ type: 'initialize', success: true });
    }
    else if (type === 'pushFrame') {
        const { frameData } = data;
        processor.pushFrame(frameData);
        // Optionally, you can send progress updates here
        // For simplicity, we're not tracking progress within the worker
    }
    else if (type === 'finish') {
        const images = processor.finishProcessing();
        self.postMessage({ type: 'finished', images });
    }
};
