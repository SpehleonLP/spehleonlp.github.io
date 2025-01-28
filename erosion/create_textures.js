
// Initialize the Web Worker
const worker = new Worker('worker.js');

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
            // 1. Extract the video file from the FileReader's result
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

            // 4. Create a canvas to capture video frames
            const canvas = new OffscreenCanvas(video.videoWidth, video.videoHeight);
            const ctx = canvas.getContext('2d', { willReadFrequently: true });
/*
            // 5. Capture the first frame and validate the top-left pixel's transparency
            await new Promise((resolve, reject) => {
                video.onseeked = () => {
                    try {
                        ctx.drawImage(video, 0, 0, canvas.width, canvas.height);
                        const firstFrame = ctx.getImageData(0, 0, canvas.width, canvas.height);
                        const topLeftAlpha = firstFrame.data[3]; // Alpha channel of the first pixel
                        if (topLeftAlpha !== 0) {
                            reject(new Error('Top-left pixel of the first frame is not transparent.'));
                        } else {
                            resolve();
                        }
                    } catch (err) {
                        reject(new Error('Error capturing the first frame.'));
                    }
                    finally {
       					video.onseeked = null; // Remove the event handler
    				}
                };
                video.onerror = () => reject(new Error('Error seeking video to the first frame.'));
                video.currentTime = 0;
            });
*/
            // 7. Initialize the worker processor
            await workerOperation('initialize', { width: canvas.width, height: canvas.height });

            console.log("initialized worker")

			const drawFrame = async () => {
                if (video.ended || video.paused) {
                    return; // Stop if video has ended or is paused
                }

                try {
                    ctx.clearRect(0, 0, canvas.width, canvas.height);
                    ctx.drawImage(video, 0, 0, canvas.width, canvas.height);
                    const frameData = ctx.getImageData(0, 0, canvas.width, canvas.height);
                    await workerOperation('pushFrame', { frameData: frameData.data.buffer }, [frameData.data.buffer]); // Send ArrayBuffer
                } catch (error) {
                    console.error('Error processing frame:', error);
                }

                requestAnimationFrame(drawFrame);
            };

// Start processing frames
            video.play();
            console.log("Started frame processing");

            drawFrame(); // Start the frame loop

            // Wait for the video to finish playing
            await new Promise((resolve) => {
                video.onended = resolve;
            });

            console.log("Finished first pass");

            await workerOperation('finishPass', {});


            console.log("Beginning second pass");

            // Reset video to start for second pass
            video.currentTime = 0;

            // Start processing frames again
            video.play();
            drawFrame(); // Start the frame loop again

            // Wait for the second pass to finish
            await new Promise((resolve) => {
                video.onended = resolve;
            });

            console.log("Finished second pass");
            // */

            await workerOperation('finishPass', {});

			let { erosion, gradient } = await workerOperation('finish');

            console.log("finished; pushing textures")

       		UploadTexture(glContext, textures.u_erosionTexture, 'erosionTextureDrop', 0, erosion);
 		    UploadTexture(glContext, textures.u_gradient, 'gradientDrop', 1, gradient);
        });

        // Optionally, handle success (e.g., hide progress screen if not handled by runWithProgress)
        console.log('Video processing completed successfully.');
    } catch (error) {
        // Handle errors (e.g., display error message to the user)
        console.error('Error processing video:', error);
        alert(`Error processing video: ${error.message}`);
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
            if (file.type.startsWith('video/')) {
                const reader = new FileReader();
                reader.onload = function(event) {
                  	ProcessVideo(event, file.type);
                };
                reader.readAsArrayBuffer(file);
            } else {
                alert('Please drop an video file.');
            }
        }
    });

    videoButton.addEventListener('click', () => {
        const input = document.createElement('input');
        input.type = 'file';
        input.accept = 'video/*';
        input.onchange = e => {
            const file = e.target.files[0];
            if (file && file.type.startsWith('video/')) {
                const reader = new FileReader();
                reader.onload = function(event) {
                  	ProcessVideo(event, file.type);
                };
                reader.readAsArrayBuffer(file);
            } else {
                alert('Please select an video file.');
            }
        }
        input.click();
    });
});
