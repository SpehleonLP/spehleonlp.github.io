
// Initialize the Web Worker
const worker = new Worker('worker.js');

// Placeholder for the WASM processor
let wasmProcessor = null;

// Listen for messages from the worker
worker.onmessage = function(e) {
    const { type, success, error, images } = e.data;

    if (type === 'init') {
        if (success) {
            console.log('Worker initialized successfully.');
        } else {
            console.error('Worker failed to initialize:', error);
            alert(`Worker initialization failed: ${error}`);
        }
    }
    else if (type === 'initialize') {
        if (success) {
            console.log('Processor initialized.');
        } else {
            console.error('Processor initialization failed.');
            alert('Processor initialization failed.');
        }
    }
    else if (type === 'finished') {
        const { erosion, gradient } = images;
        // Upload textures
        UploadTexture(glContext, textures.u_erosionTexture, 'erosionTextureDrop', 0, erosion);
        UploadTexture(glContext, textures.u_gradient, 'gradientDrop', 1, gradient);
        console.log('Processing completed successfully.');
    }
};

// Function to process the video
async function ProcessVideo(event, mimeType) {
    try {
        // Start the progress/loading screen
        await runWithProgress(async () => {
            // 1. Extract the video file from the FileReader's result
            const arrayBuffer = event.target.result;
            const blob = new Blob([arrayBuffer], { type: mimeType }); // Adjust MIME type if necessary

            // 2. Create a video element to load the video
            const video = document.createElement('video');
            video.src = URL.createObjectURL(blob);
            video.muted = true; // Mute the video to allow autoplay
            video.playsInline = true; // Ensure it plays inline on mobile
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
            const canvas = document.createElement('canvas');
            canvas.width = video.videoWidth;
            canvas.height = video.videoHeight;
            const ctx = canvas.getContext('2d');

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
                };
                video.onerror = () => reject(new Error('Error seeking video to the first frame.'));
                video.currentTime = 0;
            });

            // 6. Determine total frames
            // This method approximates the total frames based on frame rate and duration
            // For more accurate frame counts, you might need to parse the video file or use other techniques
            const fps = 30; // Adjust as needed or extract from video metadata if possible
            const duration = video.duration;
            const totalFrames = Math.ceil(duration * fps);

            // 7. Initialize the worker processor
            worker.postMessage({ type: 'initialize', data: { totalFrames, width: canvas.width, height: canvas.height } });

            // 8. Wait for the worker to initialize
            await new Promise((resolve, reject) => {
                const handleMessage = (e) => {
                    if (e.data.type === 'initialize' && e.data.success) {
                        worker.removeEventListener('message', handleMessage);
                        resolve();
                    }
                    else if (e.data.type === 'initialize' && !e.data.success) {
                        worker.removeEventListener('message', handleMessage);
                        reject(new Error('Processor initialization failed.'));
                    }
                };
                worker.addEventListener('message', handleMessage);
            });

            // 9. Play the video
            video.play();

            // 10. Capture frames as the video plays
            await new Promise((resolve, reject) => {
                video.onplay = () => {
                    const captureFrame = () => {
                        if (video.paused || video.ended) {
                            resolve();
                            return;
                        }

                        // Draw the current frame onto the canvas
                        ctx.drawImage(video, 0, 0, canvas.width, canvas.height);
                        const frame = ctx.getImageData(0, 0, canvas.width, canvas.height);

                        // Send the frame data to the worker
                        worker.postMessage({ type: 'pushFrame', data: { frameData: frame.data.buffer } }, [frame.data.buffer]);

                        // Update progress (optional)
                        // You can calculate progress based on currentTime and duration
                        // For example:
                        const progress = video.currentTime / duration;
                        // Optionally, send progress updates to runWithProgress if needed

                        // Schedule the next frame capture
                        requestIdleCallback(captureFrame);
                    };

                    captureFrame();
                };

                video.onerror = () => reject(new Error('Error during video playback while capturing frames.'));
                video.onended = () => resolve();
            });

            // 11. Finish processing
            worker.postMessage({ type: 'finish' });
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
