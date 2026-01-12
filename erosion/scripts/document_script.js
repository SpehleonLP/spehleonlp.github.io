  // Get DOM elements
        const fadeInSlider = document.getElementById('fadeInDuration');
        const fadeInValue = document.getElementById('fadeInDurationValue');
        const fadeOutSlider = document.getElementById('fadeOutDuration');
        const fadeOutValue = document.getElementById('fadeOutDurationValue');
        const transitionSlider = document.getElementById('transitionDuration');
        const transitionValue = document.getElementById('transitionDurationValue');
        const lifetimeSlider = document.getElementById('lifetime');
        const lifetimeValue = document.getElementById('lifetimeValue');
        const rateSlider = document.getElementById('rate');
        const rateValue = document.getElementById('rateValue');

        const erosionDrop = document.getElementById('erosionTextureDrop');
        const gradientDrop = document.getElementById('gradientDrop');

        const timeSlider = document.getElementById('time-slider');
        const playPauseButton = document.getElementById('play-pause');

        let lifetime = parseFloat(lifetimeSlider.value);
        let time = 0;
        let isPlaying = false;
        let animationFrameId;
        let lastTimestamp = null;

        // Update slider display values
        function updateSliderDisplay(slider, display) {
            display.textContent = slider.value;
        }

        fadeInSlider.addEventListener('input', () => {
            updateSliderDisplay(fadeInSlider, fadeInValue);
        });

        fadeOutSlider.addEventListener('input', () => {
            updateSliderDisplay(fadeOutSlider, fadeOutValue);
        });

        transitionSlider.addEventListener('input', () => {
            updateSliderDisplay(transitionSlider, transitionValue);
        });

        lifetimeSlider.addEventListener('input', () => {
            lifetime = parseFloat(lifetimeSlider.value);
            lifetimeValue.textContent = lifetime;
            timeSlider.max = lifetime;
        });

        rateSlider.addEventListener('input', () => {
            rateValue.textContent = rateSlider.value;
        });

        // Initialize lifetime display and time slider max
        updateSliderDisplay(lifetimeSlider, lifetimeValue);
        timeSlider.max = lifetime;

        // Play/Pause functionality
        playPauseButton.addEventListener('click', () => {
            isPlaying = !isPlaying;
            playPauseButton.textContent = isPlaying ? 'Pause' : 'Play';
            if (isPlaying) {
                lastTimestamp = null; // Reset timestamp
                animationFrameId = requestAnimationFrame(updateTime);
            } else {
                cancelAnimationFrame(animationFrameId);
            }
        });


        function updateTime(timestamp) {
            if (!lastTimestamp) lastTimestamp = timestamp;
            const delta = (timestamp - lastTimestamp) / 1000; // Convert to seconds
            lastTimestamp = timestamp;

            // Adjust time based on rate slider (normalized to [0, 2] where 1 is normal speed)
            const rateFactor = parseFloat(rateSlider.value); // rate=50 -> 1x speed
            time += delta * rateFactor;
            time = time % lifetime;
            timeSlider.value = time.toFixed(2);

            if (isPlaying) {
                animationFrameId = requestAnimationFrame(updateTime);
            }
        }
