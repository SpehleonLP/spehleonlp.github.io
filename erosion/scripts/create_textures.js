
// Initialize the Web Worker
const worker = new Worker('scripts/worker.js');

let w_messageCounter = 0;
const workerOperation = (type, data) => {
    return new Promise((resolve, reject) => {
        const messageId = `msg_${w_messageCounter++}`;

        const handler = (e) => {
            if (e.data.id === messageId) {
                worker.removeEventListener('message', handler);
                if (e.data.type === 'ERROR') {
                    reject(new Error(e.data.data));
                } else {
                    resolve(e.data.data);
                }
            }
        };

        worker.addEventListener('message', handler);
        worker.postMessage({ id: messageId, type, data });
    });
};


// Function to process the video
async function ProcessVideo(event, mimeType) {

    try {
        // Start the progress/loading screen
        await runWithProgress(async () => {
		    const arrayBuffer = event.target.result;
			const blob = new Blob([arrayBuffer], { type: mimeType });

			// 2. Create a video element to load the video
			const video = document.createElement('video');
			video.src = URL.createObjectURL(blob);
			video.muted = true; // Mute the video to allow ays inline on mobile
			video.loop = false;
			video.preload = 'auto';
			video.crossOrigin = 'anonymous'; // Handle cross-origin if needed

			// 3. Wait for the video metadata to load
			await new Promise((resolve, reject) => {
				video.onloadedmetadata = () => {
				    video.currentTime = 0; // Start at the first frame
				    resolve();
				};
				video.onerror = () => reject(new Error('Failed to load video metadata'));
			});

// Estimate or set the frame rate
            const estimatedFrameRate = 30; // Replace with actual frame rate if known
            const frameInterval = 1 / estimatedFrameRate; // in seconds

            const totalDuration = video.duration; // in seconds
            const totalFrames = Math.floor(totalDuration * estimatedFrameRate);

            console.log(`Total duration: ${totalDuration}s, Estimated frame rate: ${estimatedFrameRate}fps, Total frames: ${totalFrames}`);

            // 4. Create a canvas to capture video frames
            const canvas = new OffscreenCanvas(video.videoWidth, video.videoHeight);
            const ctx = canvas.getContext('2d', { willReadFrequently: true });

			let in_flight = null;
			const wait_frame = async () => {
				if (in_flight) {
					in_flight.then(() => {
					//	console.log("Previous promise resolved");
					}).catch(err => {
						console.error("Previous promise rejected:", err);
					});
				}
			};

			// Function to process a single frame
            const processFrame = async (time) => {
                return new Promise((resolve, reject) => {
                    // Seek to the specified time
                    video.currentTime = time;

                    // Wait for the seek operation to complete
                    video.onseeked = async () => {
                        try {
                            // Draw the current frame onto the canvas
                            ctx.clearRect(0, 0, canvas.width, canvas.height);
                            ctx.drawImage(video, 0, 0, canvas.width, canvas.height);

                            // Capture the frame data
                            const frameData = ctx.getImageData(0, 0, canvas.width, canvas.height);

                            await wait_frame();
                            in_flight = workerOperation('pushFrame', { frameData: frameData.data.buffer }, [frameData.data.buffer]);
                            resolve();
                        } catch (error) {
                            reject(error);
                        }
                    };

                    video.onerror = (e) => {
                        reject(new Error('Error seeking video'));
                    };
                });
            };

            // Callback for each pass
            const pass_callback = async () => {
		        // Iterate through each frame
		        for (let frame = 0; frame < totalFrames; frame++) {
		            const time = frame * frameInterval;
		            // Ensure not to exceed the video duration
		            if (time > totalDuration) break;

		            await processFrame(time);
		        }

                await wait_frame();
            };

           await CreateTextures(video.duration, canvas.width, canvas.height, pass_callback);


        });


        // Optionally, handle success (e.g., hide progress screen if not handled by runWithProgress)
        console.log('Video processing completed successfully.');
    } catch (error) {
        // Handle errors (e.g., display error message to the user)
        console.error('Error processing video:', error);
        alert(`Error processing video: ${error.message}`);
    }
}

async function CreateTextures(duration, width, height, pass_callback)
{
	try
	{
        lifetimeSlider.value = (lifetime = duration);
        lifetimeValue.textContent = parseFloat(duration);
		timeSlider.max = duration;

 // Initialize the worker processor
        console.log("initialized worker");

        await workerOperation('initialize', { width:width, height:height });

        console.log("beginning first pass");
		await pass_callback();

        let erosion = await workerOperation('finishPushingFrames', {});
        const { fadeInDuration , fadeOutDuration } = await workerOperation('GetMetadata', {});
        UploadTexture(glContext, textures.u_erosionTexture, 'erosionTextureDrop', 0, erosion);

        fadeInSlider.value = (duration * fadeInDuration);
        fadeOutSlider.value = (duration * fadeOutDuration);

        fadeInValue.textContent = parseFloat(duration * fadeInDuration);
        fadeOutValue.textContent = parseFloat(duration * fadeOutDuration);

		let gradient = await workerOperation('computeGradient', {});
        UploadTexture3D(glContext, textures.u_gradient, textures.u_gradient3D, 'gradientDrop', 1, 2, gradient.buffer, gradient.width, gradient.height, gradient.depth);

		gradient = await workerOperation('computeGradient', {});
        UploadTexture3D(glContext, textures.u_gradient, textures.u_gradient3D, 'gradientDrop', 1, 2, gradient.buffer, gradient.width, gradient.height, gradient.depth);

		let a = await workerOperation('shutdownAndRelease');


		return a;
	}
	catch(error)
	{
	// finish calls this implicitly.
		await workerOperation('shutdownAndRelease');
        throw new Error(`Failed to in processing video: ${error.message}`);
	}
}

async function ProcessVideo_new(event, mimeType) {

    try {
        await runWithProgress(async () => {
            // Initialize FFmpeg worker if needed
            const { data, width, height, frameSize, nb_frames } = await GetVideoContents_ffmpeg(event, mimeType);

			return await CreateTextures(width, height, async() => {
		        for(let i = 0; i < nb_frames; i++) {
		            const frameData = data.slice(i * frameSize, (i + 1) * frameSize);
		            await workerOperation('pushFrame', { frameData: frameData.buffer }, [frameData.buffer]);
		        }
			});
        });

        console.log('Video processing completed successfully.');
    } catch (error) {
        console.error('Error processing video:', error);
        alert(`Error processing video: ${error.message}`);
    }
}

// Function to process GIF files
async function ProcessGIF(event) {
    try {
        await runWithProgress(async () => {
            const arrayBuffer = event.target.result;
            const byteArray = new Uint8Array(arrayBuffer);

            // Parse GIF using omggif
            const gifReader = new GifReader(byteArray);
            const numFrames = gifReader.numFrames();
            const width = gifReader.width;
            const height = gifReader.height;

            console.log(`GIF: ${width}x${height}, ${numFrames} frames`);

            if (numFrames === 0) {
                throw new Error('GIF has no frames');
            }

            // Calculate duration based on frame delays
            let totalDelay = 0;
            for (let i = 0; i < numFrames; i++) {
                const frameInfo = gifReader.frameInfo(i);
                totalDelay += frameInfo.delay || 10; // Default 10 centiseconds if no delay
            }
            const duration = totalDelay / 100; // Convert centiseconds to seconds

            console.log(`Total duration: ${duration}s`);

            // Create canvas for frame composition
            const canvas = new OffscreenCanvas(width, height);
            const ctx = canvas.getContext('2d');

            // Buffer to store the current composed frame
            let previousFrameData = new Uint8ClampedArray(width * height * 4);
            previousFrameData.fill(0); // Start with transparent

            const pass_callback = async () => {
                for (let frameIndex = 0; frameIndex < numFrames; frameIndex++) {
                    const frameInfo = gifReader.frameInfo(frameIndex);

                    // Decode frame to RGBA pixels
                    const frameRGBA = new Uint8ClampedArray(width * height * 4);
                    gifReader.decodeAndBlitFrameRGBA(frameIndex, frameRGBA);

                    // Handle frame disposal method
                    if (frameIndex > 0) {
                        const prevFrameInfo = gifReader.frameInfo(frameIndex - 1);

                        // Disposal method 2: restore to background (transparent)
                        if (prevFrameInfo.disposal === 2) {
                            previousFrameData.fill(0);
                        }
                        // Disposal method 3: restore to previous (keep previousFrameData as is)
                        // Disposal method 0 or 1: no disposal (overlay on previous)
                    }

                    // Composite this frame onto the previous frame
                    const x = frameInfo.x || 0;
                    const y = frameInfo.y || 0;
                    const frameWidth = frameInfo.width || width;
                    const frameHeight = frameInfo.height || height;

                    // If this frame covers the entire canvas, just use it
                    if (x === 0 && y === 0 && frameWidth === width && frameHeight === height) {
                        for (let i = 0; i < frameRGBA.length; i++) {
                            previousFrameData[i] = frameRGBA[i];
                        }
                    } else {
                        // Composite partial frame
                        for (let py = 0; py < frameHeight; py++) {
                            for (let px = 0; px < frameWidth; px++) {
                                const srcIdx = (py * width + px) * 4;
                                const dstIdx = ((y + py) * width + (x + px)) * 4;

                                if (dstIdx >= 0 && dstIdx < previousFrameData.length) {
                                    const alpha = frameRGBA[srcIdx + 3];
                                    if (alpha > 0) {
                                        previousFrameData[dstIdx] = frameRGBA[srcIdx];
                                        previousFrameData[dstIdx + 1] = frameRGBA[srcIdx + 1];
                                        previousFrameData[dstIdx + 2] = frameRGBA[srcIdx + 2];
                                        previousFrameData[dstIdx + 3] = frameRGBA[srcIdx + 3];
                                    }
                                }
                            }
                        }
                    }

                    // Send the composed frame to the worker
                    const frameDataCopy = new Uint8Array(previousFrameData);
                    await workerOperation('pushFrame', {
                        frameData: frameDataCopy.buffer
                    }, [frameDataCopy.buffer]);
                }
            };

            await CreateTextures(duration, width, height, pass_callback);
        });

        console.log('GIF processing completed successfully.');
    } catch (error) {
        console.error('Error processing GIF:', error);
        alert(`Error processing GIF: ${error.message}`);
    }
}


document.addEventListener('DOMContentLoaded', () => {
    const videoButton = document.getElementById('load-video');
	const dropArea = document.getElementById('glCanvas');

    dropArea.addEventListener('dragover', (e) => {
        e.preventDefault();
        dropArea.style.borderColor = '#0f0';
    });

    dropArea.addEventListener('dragleave', () => {
        dropArea.style.borderColor = '#fff';
    });

    dropArea.addEventListener('drop', (e) => {
        e.preventDefault();
        dropArea.style.borderColor = '#fff';
        const files = e.dataTransfer.files;
        if (files.length > 0) {
            const file = files[0];
            const reader = new FileReader();

            if (file.type.startsWith('video/')) {
                reader.onload = function(event) {
                  	ProcessVideo(event, file.type);
                };
                reader.readAsArrayBuffer(file);
            } else if (file.type === 'image/gif') {
                reader.onload = function(event) {
                  	ProcessGIF(event);
                };
                reader.readAsArrayBuffer(file);
            } else {
                alert('Please drop a video or GIF file.');
            }
        }
    });

    videoButton.addEventListener('click', () => {
        const input = document.createElement('input');
        input.type = 'file';
        input.accept = 'video/*,image/gif';
        input.onchange = e => {
            const file = e.target.files[0];
            if (!file) return;

            const reader = new FileReader();

            if (file.type.startsWith('video/')) {
                reader.onload = function(event) {
                  	ProcessVideo(event, file.type);
                };
                reader.readAsArrayBuffer(file);
            } else if (file.type === 'image/gif') {
                reader.onload = function(event) {
                  	ProcessGIF(event);
                };
                reader.readAsArrayBuffer(file);
            } else {
                alert('Please select a video or GIF file.');
            }
        }
        input.click();
    });
});
