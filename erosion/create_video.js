// create_video.js

function assert(condition, message) {
    if (!condition) {
        throw message || "Assertion failed";
    }
}

function glDebug()
{
	const error = glContext.getError();
	if (error !== glContext.NO_ERROR) {
		console.error('WebGL Error:', error);
	}
}

// Ensure the DOM is fully loaded
document.addEventListener('DOMContentLoaded', () => {
    const saveVideoButton = document.getElementById('save-video');

    if (saveVideoButton) {
        saveVideoButton.addEventListener('click', async () => {
            try {
            	is_making_video = true;
                await runWithProgress(createVideo);
          //      alert('Video saved successfully!');
            } catch (error) {
                alert('An error occurred while saving the video.');
                console.error(error);
            }

            is_making_video = false;
        });
    } else {
        console.error('Save Video button not found.');
    }
});
/**
 * Initializes an off-screen framebuffer with an alpha channel and a depth buffer.
 * @param {number} width - The width of the framebuffer.
 * @param {number} height - The height of the framebuffer.
 * @returns {Object} - An object containing the framebuffer, texture, and depth renderbuffer.
 */
function initializeOffscreenFramebuffer(width, height) {
	assert(width > 0 && height > 0, "buffer size 0");

    // Create framebuffer
    const framebuffer = glContext.createFramebuffer();
    glContext.bindFramebuffer(glContext.FRAMEBUFFER, framebuffer);

    // Create color texture
    const texture = glContext.createTexture();
    glContext.bindTexture(glContext.TEXTURE_2D, texture);
    glContext.texImage2D(
        glContext.TEXTURE_2D,
        0,
        glContext.RGBA,
        width,
        height,
        0,
        glContext.RGBA,
        glContext.UNSIGNED_BYTE,
        null
    );
    glContext.texParameteri(glContext.TEXTURE_2D, glContext.TEXTURE_MIN_FILTER, glContext.NEAREST);
    glContext.texParameteri(glContext.TEXTURE_2D, glContext.TEXTURE_MAG_FILTER, glContext.NEAREST);
    glContext.texParameteri(glContext.TEXTURE_2D, glContext.TEXTURE_WRAP_S, glContext.CLAMP_TO_EDGE);
    glContext.texParameteri(glContext.TEXTURE_2D, glContext.TEXTURE_WRAP_T, glContext.CLAMP_TO_EDGE);
    glContext.bindTexture(glContext.TEXTURE_2D, null);

    // Create and attach depth renderbuffer
    const depthRenderbuffer = glContext.createRenderbuffer();
    glContext.bindRenderbuffer(glContext.RENDERBUFFER, depthRenderbuffer);
    glContext.renderbufferStorage(
        glContext.RENDERBUFFER,
        glContext.DEPTH_COMPONENT16,
        width,
        height
    );

    glContext.bindRenderbuffer(glContext.RENDERBUFFER, null);

    // Attach color texture to framebuffer
    glContext.framebufferTexture2D(
        glContext.FRAMEBUFFER,
        glContext.COLOR_ATTACHMENT0,
        glContext.TEXTURE_2D,
        texture,
        0
    );

    glContext.framebufferRenderbuffer(
        glContext.FRAMEBUFFER,
        glContext.DEPTH_ATTACHMENT,
        glContext.RENDERBUFFER,
        depthRenderbuffer
    );

    glContext.disable(glContext.DEPTH_TEST);
    glContext.disable(glContext.STENCIL_TEST);


    // Check framebuffer status
    const status = glContext.checkFramebufferStatus(glContext.FRAMEBUFFER);
    if (status !== glContext.FRAMEBUFFER_COMPLETE) {
        console.error('Framebuffer is not complete:', status == 0x8cd6? "INCOMPLETE_ATTACHMENT" : `0x${status.toString(16)}`);

   		glContext.bindFramebuffer(glContext.FRAMEBUFFER, null);

        // Optionally, you can clean up the created resources here
        glContext.deleteFramebuffer(framebuffer);
        glContext.deleteTexture(texture);
        glContext.deleteRenderbuffer(depthRenderbuffer);
        return null;
    }

   	glContext.bindFramebuffer(glContext.FRAMEBUFFER, null);

    return {
        framebuffer: framebuffer,
        texture: texture,
        depthRenderbuffer: depthRenderbuffer,
        width: width,
        height: height
    };
}


/**
 * Initializes and configures the VideoEncoder.
 * @returns {VideoEncoder} - The configured VideoEncoder instance.
 */
function initializeVideoEncoder(width, height, fps, outputBuffer) {
    const init = {
	  output: (chunk, metadata) => {
			outputBuffer.push(chunk.data);
        },
	  error: (e) => {
		console.log(e.message);
	  },
	};

    const config = {
        codec: 'vp8', // VP8 supports alpha in WebM
        width: width,
        height: height,
        framerate: fps,
        bitrate: width*height * fps * 32 / 1000,
        hardwareAcceleration: 'prefer-hardware',
        alpha: "keep"
    };

	encoder = new VideoEncoder(init);

	VideoEncoder.isConfigSupported(config).then(supported => {
		if (supported) {
		  encoder.configure(config);
		} else {
		  throw "config failed";
		}
	});

    return encoder;
}


/**
 * Captures a single frame from the off-screen framebuffer.
 * @param {number} frameNumber - The current frame number.
 * @param {WebGLFramebuffer} framebuffer - The off-screen framebuffer.
 * @returns {ImageBitmap} - The captured frame as an ImageBitmap.
 */
function captureFrame(frameNumber, framebuffer) {
    // Calculate the current time in seconds
    const currentTime = frameNumber / 30; // Assuming 30 FPS


    // Bind the framebuffer for reading
    glContext.bindFramebuffer(glContext.FRAMEBUFFER, framebuffer);

    // Render the frame to the off-screen framebuffer
    renderDelegate(currentTime, true); // isInFrameBuffer = true

    // Read pixels from the framebuffer
    const pixels = new Uint8Array(recordCanvasWidth * recordCanvasHeight * 4);
    glContext.readPixels(0, 0, recordCanvasWidth, recordCanvasHeight, glContext.RGBA, glContext.UNSIGNED_BYTE, pixels);

	const id = ((recordCanvasHeight / 2) * recordCanvasWidth +  recordCanvasWidth / 2) * 4;

    // Log the first few pixel values for debugging
    console.log(`Frame ${frameNumber + 1}: First pixel RGBA: ${pixels[id+0]}, ${pixels[id+1]}, ${pixels[id+2]}, ${pixels[id+3]}`);

    // Unbind the framebuffer
    glContext.bindFramebuffer(glContext.FRAMEBUFFER, null);

    // Create ImageData from pixel data
    return new ImageData(new Uint8ClampedArray(pixels), recordCanvasWidth, recordCanvasHeight);;
}

/**
 * Creates the video by capturing frames and encoding them.
 */
async function createVideo() {
    if (!glContext) {
        throw new Error('WebGL context (glContext) is not initialized.');
    }

    if (typeof renderDelegate !== 'function') {
        throw new Error('renderDelegate is not defined or not a function.');
    }

    // Retrieve video settings
    const lifetimeSlider = document.getElementById('lifetime');
    const lifetime = parseFloat(lifetimeSlider.value); // in seconds
    const fps = 30;
    const totalFrames = Math.floor(lifetime * fps);

    // Define video dimensions
    const width = recordCanvasWidth;
    const height = recordCanvasHeight;

    // Initialize off-screen framebuffer
    const offscreenFramebuffer = initializeOffscreenFramebuffer(recordCanvasWidth, recordCanvasHeight);

	var videoWriter = new WebMWriter({
		quality: 0.99999,    // WebM image quality from 0.0 (worst) to 0.99999 (best), 1.00 (VP8L lossless) is not supported
		fileWriter: null, // FileWriter in order to stream to a file instead of buffering to memory (optional)
		fd: null,         // Node.js file handle to write to instead of buffering to memory (optional)

		// You must supply one of:
		frameDuration: null, // Duration of frames in milliseconds
		frameRate: fps,     // Number of frames per second

		transparent: true,      // True if an alpha channel should be included in the video
		alphaQuality: 0.99999, // Allows you to set the quality level of the alpha channel separately.
		                         // If not specified this defaults to the same value as `quality`.
	});


    // Start the encoder
//    encoder.start();

    console.log('Video encoding started.');

	const tempCanvas = document.createElement('canvas');
	tempCanvas.width = recordCanvasWidth;
	tempCanvas.height = recordCanvasHeight;
	tempCanvas.style.display = 'none';

	const tempCtx = tempCanvas.getContext('2d');
	document.body.appendChild(tempCanvas);

    // Capture and encode each frame
    for (let frame = 0; frame < totalFrames; frame++) {
        const imageData = await captureFrame(frame, offscreenFramebuffer.framebuffer);
	   	tempCtx.putImageData(imageData, 0, 0);

        videoWriter.addFrame(tempCanvas, { timestamp: (frame * 1000) / fps });
    }

    console.log('All frames captured and encoded. Flushing encoder.');

    // Flush and close the encoder

    videoWriter.complete().then(function(webMBlob) {
		// Create a download link and trigger it
		const downloadLink = document.createElement('a');
		downloadLink.href = URL.createObjectURL(webMBlob);
		downloadLink.download = 'recorded_video.webm';
		downloadLink.textContent = 'Download Video';

		// Append the link to the body
		document.body.appendChild(downloadLink);

		// Automatically trigger the download
		downloadLink.click();

		// Clean up by removing the download link
		document.body.removeChild(downloadLink);
    	document.body.removeChild(tempCanvas);

		console.log('Video download initiated.');
	});
}
