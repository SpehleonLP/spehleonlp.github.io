// Sensor data storage
const sensorData = {
    orientation: { alpha: 0, beta: 0, gamma: 0 },
    acceleration: { x: 0, y: 0, z: 0 },
    gyroscope: { x: 0, y: 0, z: 0 },
    velocity: { x: 0, y: 0, z: 0 },
    position: { x: 0, y: 0, z: 0 },
    gps: {
        speed: null,
        heading: null,
        altitude: null,
        latitude: null,
        longitude: null,
        accuracy: null
    }
};

// Integration tracking
let lastTimestamp = null;
let integrationActive = false;

// DOM elements
const startButton = document.getElementById('start-sensors');
const gpsButton = document.getElementById('enable-gps');
const resetButton = document.getElementById('reset-integration');
const sensorStatus = document.getElementById('sensor-status');
const gpsStatus = document.getElementById('gps-status');

let sensorsActive = false;
let gpsWatchId = null;
let gpsActive = false;

// Check sensor availability
function checkSensorSupport() {
    console.log('Checking sensor support...');
    console.log('DeviceOrientationEvent:', typeof DeviceOrientationEvent);
    console.log('DeviceMotionEvent:', typeof DeviceMotionEvent);

    if (typeof DeviceOrientationEvent !== 'undefined' && typeof DeviceMotionEvent !== 'undefined') {
        sensorStatus.textContent = 'Ready';
        sensorStatus.style.color = '#10b981';

        // Auto-start if no permission needed
        if (typeof DeviceOrientationEvent.requestPermission !== 'function') {
            console.log('No permission needed, auto-starting sensors');
            startSensors();
        } else {
            console.log('Permission required, waiting for user click');
        }
    } else {
        sensorStatus.textContent = 'Not Supported';
        sensorStatus.style.color = '#ef4444';
        if (startButton) {
            startButton.disabled = true;
        }
    }
}

// Request sensor permissions and start tracking
async function startSensors() {
    console.log('startSensors() called');
    try {
        // Request permission for iOS 13+
        if (typeof DeviceOrientationEvent !== 'undefined' &&
            typeof DeviceOrientationEvent.requestPermission === 'function') {

            console.log('Requesting device orientation permission...');
            const orientationPermission = await DeviceOrientationEvent.requestPermission();
            console.log('Permission result:', orientationPermission);

            if (orientationPermission !== 'granted') {
                sensorStatus.textContent = 'Permission Denied';
                sensorStatus.style.color = '#ef4444';
                return;
            }
        }

        // Start listening to sensors
        console.log('Adding sensor event listeners...');
        window.addEventListener('deviceorientation', handleOrientation);
        window.addEventListener('devicemotion', handleMotion);

        sensorsActive = true;
        sensorStatus.textContent = 'Active';
        sensorStatus.style.color = '#10b981';
        if (startButton) {
            startButton.textContent = 'Sensors Active';
            startButton.disabled = true;
        }

        console.log('Sensors activated successfully');

    } catch (error) {
        console.error('Error requesting sensor permissions:', error);
        sensorStatus.textContent = 'Error';
        sensorStatus.style.color = '#ef4444';
    }
}

// Handle device orientation
function handleOrientation(event) {
    // Check if we're getting real sensor data (not null)
    if (event.alpha !== null || event.beta !== null || event.gamma !== null) {
        sensorData.orientation.alpha = event.alpha || 0;
        sensorData.orientation.beta = event.beta || 0;
        sensorData.orientation.gamma = event.gamma || 0;
        updateSensorDisplay();

        // Update status on first real data
        if (sensorStatus.textContent === 'Ready' || sensorStatus.textContent === 'Active') {
            sensorStatus.textContent = 'Active';
            sensorStatus.style.color = '#10b981';
        }
    } else {
        console.log('No orientation data (device may not have sensors)');
    }
}

// Handle device motion
function handleMotion(event) {
    const currentTimestamp = Date.now();

    if (event.acceleration) {
        sensorData.acceleration.x = event.acceleration.x || 0;
        sensorData.acceleration.y = event.acceleration.y || 0;
        sensorData.acceleration.z = event.acceleration.z || 0;

        // Perform integration if we have a previous timestamp
        if (lastTimestamp !== null && integrationActive) {
            const deltaTime = (currentTimestamp - lastTimestamp) / 1000; // Convert to seconds
            integrateAcceleration(deltaTime);
        }
    }

    if (event.rotationRate) {
        sensorData.gyroscope.x = event.rotationRate.alpha || 0;
        sensorData.gyroscope.y = event.rotationRate.beta || 0;
        sensorData.gyroscope.z = event.rotationRate.gamma || 0;
    }

    lastTimestamp = currentTimestamp;
    updateSensorDisplay();
}

// Integrate acceleration to get velocity, then integrate velocity to get position
function integrateAcceleration(deltaTime) {
    // Velocity integration: v = v0 + a * dt
    sensorData.velocity.x += sensorData.acceleration.x * deltaTime;
    sensorData.velocity.y += sensorData.acceleration.y * deltaTime;
    sensorData.velocity.z += sensorData.acceleration.z * deltaTime;

    // Position integration: p = p0 + v * dt
    sensorData.position.x += sensorData.velocity.x * deltaTime;
    sensorData.position.y += sensorData.velocity.y * deltaTime;
    sensorData.position.z += sensorData.velocity.z * deltaTime;

    // Update integrated displays
    updateIntegratedDisplay();
}

// Reset integrated velocity and position
function resetIntegration() {
    console.log('Resetting integration...');
    sensorData.velocity.x = 0;
    sensorData.velocity.y = 0;
    sensorData.velocity.z = 0;
    sensorData.position.x = 0;
    sensorData.position.y = 0;
    sensorData.position.z = 0;
    lastTimestamp = null;
    integrationActive = true;
    updateIntegratedDisplay();
}

// Update sensor display
function updateSensorDisplay() {
    // Orientation
    updateElement('alpha', sensorData.orientation.alpha);
    updateElement('beta', sensorData.orientation.beta);
    updateElement('gamma', sensorData.orientation.gamma);

    // Acceleration
    updateElement('accel-x', sensorData.acceleration.x, ' m/s²');
    updateElement('accel-y', sensorData.acceleration.y, ' m/s²');
    updateElement('accel-z', sensorData.acceleration.z, ' m/s²');

    // Gyroscope
    updateElement('gyro-x', sensorData.gyroscope.x);
    updateElement('gyro-y', sensorData.gyroscope.y);
    updateElement('gyro-z', sensorData.gyroscope.z);
}

// Update integrated velocity and position display
function updateIntegratedDisplay() {
    // Integrated Velocity
    updateElement('vel-x', sensorData.velocity.x, ' m/s');
    updateElement('vel-y', sensorData.velocity.y, ' m/s');
    updateElement('vel-z', sensorData.velocity.z, ' m/s');

    // Integrated Position
    updateElement('pos-x', sensorData.position.x, ' m');
    updateElement('pos-y', sensorData.position.y, ' m');
    updateElement('pos-z', sensorData.position.z, ' m');
}

// Enable GPS tracking
async function enableGPS() {
    console.log('enableGPS() called');

    if (!navigator.geolocation) {
        console.log('Geolocation not supported');
        gpsStatus.textContent = 'Not Supported';
        gpsStatus.style.color = '#ef4444';
        if (gpsButton) {
            gpsButton.disabled = true;
        }
        return;
    }

    // On iOS, we need to use getCurrentPosition first to trigger permission prompt
    // watchPosition doesn't always trigger the prompt on iOS Safari
    console.log('Requesting initial position to trigger permission...');

    try {
        // First, get current position to trigger permission prompt
        navigator.geolocation.getCurrentPosition(
            (position) => {
                console.log('Initial position acquired, starting watch...');
                // Now start watching for continuous updates
                startGPSWatch();
            },
            (error) => {
                console.error('Initial GPS error:', error);
                handleGPSError(error);
            },
            {
                enableHighAccuracy: true,
                maximumAge: 0,
                timeout: 10000
            }
        );

        gpsStatus.textContent = 'Requesting Permission...';
        gpsStatus.style.color = '#f59e0b';
        if (gpsButton) {
            gpsButton.textContent = 'GPS Acquiring...';
            gpsButton.disabled = true;
        }

    } catch (error) {
        console.error('Error enabling GPS:', error);
        gpsStatus.textContent = 'Error';
        gpsStatus.style.color = '#ef4444';
    }
}

// Start continuous GPS watching
function startGPSWatch() {
    console.log('Starting GPS watch...');
    gpsWatchId = navigator.geolocation.watchPosition(
        handleGPSSuccess,
        handleGPSError,
        {
            enableHighAccuracy: true,  // Use GPS for best accuracy
            maximumAge: 0,              // Don't use cached position
            timeout: 10000              // 10 second timeout
        }
    );

    gpsStatus.textContent = 'Acquiring...';
    gpsStatus.style.color = '#f59e0b';
}

// Handle successful GPS position
function handleGPSSuccess(position) {
    gpsActive = true;

    // Update GPS data
    sensorData.gps.latitude = position.coords.latitude;
    sensorData.gps.longitude = position.coords.longitude;
    sensorData.gps.accuracy = position.coords.accuracy;

    // Speed in m/s (from GPS Doppler effect)
    sensorData.gps.speed = position.coords.speed;

    // Heading in degrees (0-360, true north)
    sensorData.gps.heading = position.coords.heading;

    // Altitude in meters
    sensorData.gps.altitude = position.coords.altitude;

    // Update status
    gpsStatus.textContent = 'Active';
    gpsStatus.style.color = '#10b981';
    gpsButton.textContent = 'GPS Active';

    // Update display
    updateGPSDisplay();
}

// Handle GPS error
function handleGPSError(error) {
    let errorMsg = 'Error';
    let errorDetail = '';

    switch(error.code) {
        case error.PERMISSION_DENIED:
            errorMsg = 'Permission Denied';
            errorDetail = 'Location access was denied. On iOS: Settings > Safari > Location > Ask';
            break;
        case error.POSITION_UNAVAILABLE:
            errorMsg = 'Position Unavailable';
            errorDetail = 'Location information is unavailable. Try going outside.';
            break;
        case error.TIMEOUT:
            errorMsg = 'Timeout';
            errorDetail = 'Location request timed out. Try again.';
            break;
    }

    gpsStatus.textContent = errorMsg;
    gpsStatus.style.color = '#ef4444';

    if (gpsButton) {
        gpsButton.disabled = false;
        gpsButton.textContent = 'Enable GPS';
    }

    console.error('GPS Error:', error.message);
    console.error('Error details:', errorDetail);

    // Show alert with helpful message on permission denial
    if (error.code === error.PERMISSION_DENIED) {
        alert('Location access denied.\n\nOn iOS:\n1. Go to Settings > Safari > Location\n2. Choose "Ask" or "Allow"\n3. Refresh this page and try again');
    }
}

// Update GPS display
function updateGPSDisplay() {
    // Speed (convert to km/h if available)
    if (sensorData.gps.speed !== null) {
        const speedKmh = sensorData.gps.speed * 3.6;
        updateElement('gps-speed', speedKmh, ' km/h');
    } else {
        document.getElementById('gps-speed').textContent = 'N/A';
    }

    // Heading
    if (sensorData.gps.heading !== null) {
        updateElement('gps-heading', sensorData.gps.heading, '°');
    } else {
        document.getElementById('gps-heading').textContent = 'N/A';
    }

    // Altitude
    if (sensorData.gps.altitude !== null) {
        updateElement('gps-altitude', sensorData.gps.altitude, ' m');
    } else {
        document.getElementById('gps-altitude').textContent = 'N/A';
    }

    // Position
    if (sensorData.gps.latitude !== null) {
        updateElement('gps-lat', sensorData.gps.latitude, '°');
    }
    if (sensorData.gps.longitude !== null) {
        updateElement('gps-lon', sensorData.gps.longitude, '°');
    }
    if (sensorData.gps.accuracy !== null) {
        updateElement('gps-accuracy', sensorData.gps.accuracy, ' m');
    }
}

// Update individual element
function updateElement(id, value, suffix = '') {
    const element = document.getElementById(id);
    if (element) {
        const displayValue = typeof value === 'number' ? value.toFixed(2) : value;
        element.textContent = displayValue + suffix;
        element.classList.add('updating');
        setTimeout(() => element.classList.remove('updating'), 500);
    }
}

// Event listeners
if (startButton) {
    console.log('Adding click listener to start button');
    startButton.addEventListener('click', () => {
        console.log('Start button clicked!');
        startSensors();
    });
} else {
    console.error('Start button not found!');
}

if (gpsButton) {
    console.log('Adding click listener to GPS button');
    gpsButton.addEventListener('click', () => {
        console.log('GPS button clicked!');
        enableGPS();
    });
} else {
    console.error('GPS button not found!');
}

if (resetButton) {
    console.log('Adding click listener to reset button');
    resetButton.addEventListener('click', () => {
        console.log('Reset button clicked!');
        resetIntegration();
    });
} else {
    console.error('Reset button not found!');
}

// Initialize on load
window.addEventListener('load', () => {
    console.log('Page loaded, initializing...');
    checkSensorSupport();
    // Initialize integration as active
    integrationActive = true;
});

// Handle visibility change
document.addEventListener('visibilitychange', () => {
    if (!document.hidden && !sensorsActive) {
        // Re-check sensors when page becomes visible
        checkSensorSupport();
    }
});

// Clean up GPS watch when page unloads
window.addEventListener('beforeunload', () => {
    if (gpsWatchId !== null) {
        navigator.geolocation.clearWatch(gpsWatchId);
    }
});
