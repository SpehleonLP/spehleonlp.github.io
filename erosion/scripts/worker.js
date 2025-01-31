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
        this.push_frame = this.module.cwrap('push_frame', null, ['number', 'number']);
        this.finishPushingFrames = this.module.cwrap('finishPushingFrames', 'number', []);
        this.computeGradient = this.module.cwrap('computeGradient', null, []);
        this.get_image = this.module.cwrap('get_image', 'number', ['number']);
        this.GetMetadata = this.module.cwrap('GetMetadata', 'number', []);
        this.shutdownAndRelease = this.module.cwrap('shutdownAndRelease', null, []);

        console.log("push_frame function bound:", !!this.push_frame);
        console.log("finishPushingFrames function bound:", !!this.finishPushingFrames);
        console.log("get_image function bound:", !!this.get_image);
        console.log("GetMetadata function bound:", !!this.GetMetadata);
        console.log("shutdownAndRelease function bound:", !!this.shutdownAndRelease);
        console.log("computeGradient function bound:", !!this.computeGradient);
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
		} else if (frameDataBuffer instanceof Uint8Array
		|| frameDataBuffer instanceof Uint8ClampedArray) {
		    data = frameDataBuffer;
		} else {
		    throw new Error('Expected an ArrayBuffer or Uint8Array.');
		}

		const numBytes = data.length;

		const ptr = this.module._malloc(numBytes); // Allocate memory in WebAssembly
		this.module.HEAPU8.set(data, ptr);         // Copy data into WebAssembly memory
		this.push_frame(ptr, numBytes);                      // Call the C function
	//	this.module._free(ptr);                    // Free allocated memory
	}

    finishPushingFrames() {
        this.finishPushingFrames();
    }

    FetchMetadata() {
        const ptr = this.GetMetadata(1);
        const fadeInDuration = this.module.getValue(ptr, 'float');
        const fadeOutDuration = this.module.getValue(ptr + 4, 'float');
        return { fadeInDuration, fadeOutDuration };
    }


	getGradient()
	{
        this.computeGradient();

        const gradientPtr = this.get_image(1);
        const gradient = this.extract3DTexture(gradientPtr);

        return gradient;
	}

    // Extract ImageData from a pointer
    extractImageData(ptr) {
        if (ptr === 0) return null;

        const width = this.module.getValue(ptr, 'i32');
        const height = this.module.getValue(ptr + 4, 'i32');
        const depth = this.module.getValue(ptr + 8, 'i32');
        const dataPtr = this.module.getValue(ptr + 16, '*');
        const dataLength = depth * width * height * 4;
        const data = new Uint8ClampedArray(this.module.HEAPU8.buffer, dataPtr, dataLength).slice();

        return new ImageData(new Uint8ClampedArray(data), width, height * depth);
    }

    extract3DTexture(ptr) {
        if (ptr === 0) return null;

        const width = this.module.getValue(ptr, 'i32');
        const height = this.module.getValue(ptr + 4, 'i32');
        const depth = this.module.getValue(ptr + 8, 'i32');
        const dataPtr = this.module.getValue(ptr + 16, '*');
        const dataLength = depth * width * height * 4;
        const data = new Uint8ClampedArray(this.module.HEAPU8.buffer, dataPtr, dataLength).slice();

        return {buffer:new Uint8ClampedArray(data), width: width, height: height, depth: depth };
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
            case 'finishPushingFrames':
            {
                processor.finishPushingFrames();
                const erosionPtr = processor.get_image(0);
       		 	const erosion = processor.extractImageData(erosionPtr);

                self.postMessage({ id, type: 'finished', data: erosion });
            }   break;
            case 'computeGradient':
            {
                const results = processor.getGradient();
                self.postMessage({ id, type: 'computedGradient', data: results });
            }   break;
            case 'GetMetadata':
            {
                const results = processor.FetchMetadata();
                self.postMessage({ id, type: 'GotMetadata', data: results });
            }   break;
            case 'shutdownAndRelease':
                processor.shutdownAndRelease();
                self.postMessage({ id, type: 'shutdown' });
                break;
            default:
                throw new Error(`Unknown message type: ${type}`);
        }
    } catch (error) {
        self.postMessage({ id, type: 'ERROR', data: error.message });
    }
}
