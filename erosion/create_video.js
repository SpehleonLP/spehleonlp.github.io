// create_video.js

let ffmpeg_worker = null;
let ff_messageCounter = 0;

const ffmpegOperation = (type, data) => {
    return new Promise((resolve, reject) => {
        const messageId = `msg_${ff_messageCounter++}`;

        const handler = (e) => {
            if (e.data.id === messageId) {
                ffmpeg_worker.removeEventListener('message', handler);
                if (e.data.type === 'ERROR') {
                    reject(new Error(e.data.data));
                } else {
                    resolve(e.data.data);
                }
            }
        };

        ffmpeg_worker.addEventListener('message', handler);
        ffmpeg_worker.postMessage({ id: messageId, type, data });
    });
};


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
    	destroy : function()
    	{
       		glContext.deleteFramebuffer(framebuffer);
       		glContext.deleteTexture(texture);
        	glContext.deleteRenderbuffer(depthRenderbuffer);
    	},
        framebuffer: framebuffer,
        texture: texture,
        depthRenderbuffer: depthRenderbuffer,
        width: width,
        height: height
    };
}



/**
 * Captures a single frame from the off-screen framebuffer.
 * @param {number} frameNumber - The current frame number.
 * @param {WebGLFramebuffer} framebuffer - The off-screen framebuffer.
 * @returns {ImageBitmap} - The captured frame as an ImageBitmap.
 */
function captureFrame(frameNumber, framebuffer, fps) {
    // Calculate the current time in seconds
    const currentTime = frameNumber / fps; // Assuming 30 FPS


    // Bind the framebuffer for reading
    glContext.bindFramebuffer(glContext.FRAMEBUFFER, framebuffer);

    // Render the frame to the off-screen framebuffer
    renderDelegate(currentTime, true); // isInFrameBuffer = true

    // Read pixels from the framebuffer
    const pixels = new Uint8Array(recordCanvasWidth * recordCanvasHeight * 4);
    glContext.readPixels(0, 0, recordCanvasWidth, recordCanvasHeight, glContext.RGBA, glContext.UNSIGNED_BYTE, pixels);

	const id = ((recordCanvasHeight / 2) * recordCanvasWidth +  recordCanvasWidth / 2) * 4;

    // Log the first few pixel values for debugging
   // console.log(`Frame ${frameNumber + 1}: First pixel RGBA: ${pixels[id+0]}, ${pixels[id+1]}, ${pixels[id+2]}, ${pixels[id+3]}`);

    // Unbind the framebuffer
    glContext.bindFramebuffer(glContext.FRAMEBUFFER, null);

    // Create ImageData from pixel data
    return new ImageData(new Uint8ClampedArray(pixels), recordCanvasWidth, recordCanvasHeight);;
}


// Function to delete the 'frames' directory and its contents
async function deleteDirectoryWithContents(path) {
    try {
        // List all files in the directory
        const files = await ffmpegOperation('LIST_DIR', { path });

        // Delete each file
        for (const file of files) {
            await ffmpegOperation('DELETE_FILE', { path: `${path}/${file}` });
        }

        // Delete the directory itself
        await ffmpegOperation('DELETE_DIR', { path });
        console.log(`Successfully deleted directory: ${path}`);
    } catch (error) {
        console.error(`Error deleting directory ${path}:`, error);
        throw error; // Re-throw for higher-level handling
    }
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

	if(ffmpeg_worker == null)
	{
		ffmpeg_worker = new Worker('ffmpeg-worker.js');
		// Load FFmpeg
		await ffmpegOperation('LOAD', {});
	}

    // Retrieve video settings
    const lifetimeSlider = document.getElementById('lifetime');
    const lifetime = parseFloat(lifetimeSlider.value); // in seconds
    const fps = 120;
    const save_fps = 30;
    const totalFrames = Math.floor(lifetime * fps);

    // Define video dimensions
    const width = recordCanvasWidth;
    const height = recordCanvasHeight;

    // Initialize off-screen framebuffer
    const offscreenFramebuffer = initializeOffscreenFramebuffer(recordCanvasWidth, recordCanvasHeight);


    console.log('Video encoding started.');

	const tempCanvas = new OffscreenCanvas(recordCanvasWidth, recordCanvasHeight);
	const tempCtx = tempCanvas.getContext('2d');

	await ffmpegOperation('CREATE_DIR', {
            path: `frames`
        });

    // Capture and encode each frame
    for (let frame = 0; frame < totalFrames; frame++) {
        const imageData = await captureFrame(frame, offscreenFramebuffer.framebuffer, fps);
	   	tempCtx.putImageData(imageData, 0, 0);

        // Convert canvas to blob and write to FFmpeg virtual filesystem
        const blob = await tempCanvas.convertToBlob({type: 'image/png'});
        const frameData = new Uint8Array(await blob.arrayBuffer());

        await ffmpegOperation('WRITE_FILE', {
            path: `frames/frame_${frame.toString().padStart(6, '0')}.png`,
            data: frameData
        });
    }

    offscreenFramebuffer.destroy();

    console.log('All frames captured and encoded. Flushing encoder.');


    // Run FFmpeg command

    await ffmpegOperation('EXEC', {
    args: [
        '-framerate', `${save_fps}`,
        '-i', 'frames/frame_%06d.png',
        '-c:v', 'libvpx-vp9',  // Use libvpx-vp9 for VP9 encoding
        '-lossless', '1',      // Enable lossless mode
        'output.webm'          // Output file
    ]
	});
/*
    await ffmpegOperation('EXEC', {
        args: [
            '-framerate', `${save_fps}`,
            '-i', 'frames/frame_%06d.png',
            '-c:v', 'vp8',
            '-b:v', '10M',
            '-auto-alt-ref', '0',
            'output.webm'
        ]
    });*/

    console.log('encoded video, deleting temporary data.');

//	await deleteDirectoryWithContents('frames');

// doesn't reach this
    console.log('fetching data.');

    // Read the output file
    const data = await ffmpegOperation('READ_FILE', {
        path: 'output.webm'
    });

    const url = URL.createObjectURL(new Blob([data.buffer], { type: 'video/webm' }));
    const a = document.createElement('a');
    a.href = url;
    a.download = 'recorded_video.webm';
    a.click();

	console.log('Video download initiated.');
}
