// Sensor data storage
const sensorData = {
    orientation: { alpha: 0, beta: 0, gamma: 0 },
    acceleration: { x: 0, y: 0, z: 0 },
    gyroscope: { x: 0, y: 0, z: 0 },
    gps: {
        speed: null,
        heading: null,
        altitude: null,
        latitude: null,
        longitude: null,
        accuracy: null
    }
};

// DOM elements
const startButton = document.getElementById('start-sensors');
const gpsButton = document.getElementById('enable-gps');
const sensorStatus = document.getElementById('sensor-status');
const gpsStatus = document.getElementById('gps-status');

let sensorsActive = false;
let gpsWatchId = null;
let gpsActive = false;

// Check sensor availability
function checkSensorSupport() {
    if (typeof DeviceOrientationEvent !== 'undefined' && typeof DeviceMotionEvent !== 'undefined') {
        sensorStatus.textContent = 'Ready';
        sensorStatus.style.color = '#10b981';

        // Auto-start if no permission needed
        if (typeof DeviceOrientationEvent.requestPermission !== 'function') {
            startSensors();
        }
    } else {
        sensorStatus.textContent = 'Not Supported';
        sensorStatus.style.color = '#ef4444';
        startButton.disabled = true;
    }
}

// Request sensor permissions and start tracking
async function startSensors() {
    try {
        // Request permission for iOS 13+
        if (typeof DeviceOrientationEvent !== 'undefined' &&
            typeof DeviceOrientationEvent.requestPermission === 'function') {

            const orientationPermission = await DeviceOrientationEvent.requestPermission();

            if (orientationPermission !== 'granted') {
                sensorStatus.textContent = 'Permission Denied';
                sensorStatus.style.color = '#ef4444';
                return;
            }
        }

        // Start listening to sensors
        window.addEventListener('deviceorientation', handleOrientation);
        window.addEventListener('devicemotion', handleMotion);

        sensorsActive = true;
        sensorStatus.textContent = 'Active';
        sensorStatus.style.color = '#10b981';
        startButton.textContent = 'Sensors Active';
        startButton.disabled = true;

    } catch (error) {
        console.error('Error requesting sensor permissions:', error);
        sensorStatus.textContent = 'Error';
        sensorStatus.style.color = '#ef4444';
    }
}

// Handle device orientation
function handleOrientation(event) {
    sensorData.orientation.alpha = event.alpha || 0;
    sensorData.orientation.beta = event.beta || 0;
    sensorData.orientation.gamma = event.gamma || 0;
    updateSensorDisplay();
}

// Handle device motion
function handleMotion(event) {
    if (event.acceleration) {
        sensorData.acceleration.x = event.acceleration.x || 0;
        sensorData.acceleration.y = event.acceleration.y || 0;
        sensorData.acceleration.z = event.acceleration.z || 0;
    }

    if (event.rotationRate) {
        sensorData.gyroscope.x = event.rotationRate.alpha || 0;
        sensorData.gyroscope.y = event.rotationRate.beta || 0;
        sensorData.gyroscope.z = event.rotationRate.gamma || 0;
    }

    updateSensorDisplay();
}

// Update sensor display
function updateSensorDisplay() {
    // Orientation
    updateElement('alpha', sensorData.orientation.alpha);
    updateElement('beta', sensorData.orientation.beta);
    updateElement('gamma', sensorData.orientation.gamma);

    // Acceleration
    updateElement('accel-x', sensorData.acceleration.x);
    updateElement('accel-y', sensorData.acceleration.y);
    updateElement('accel-z', sensorData.acceleration.z);

    // Gyroscope
    updateElement('gyro-x', sensorData.gyroscope.x);
    updateElement('gyro-y', sensorData.gyroscope.y);
    updateElement('gyro-z', sensorData.gyroscope.z);
}

// Enable GPS tracking
async function enableGPS() {
    if (!navigator.geolocation) {
        gpsStatus.textContent = 'Not Supported';
        gpsStatus.style.color = '#ef4444';
        gpsButton.disabled = true;
        return;
    }

    try {
        // Request location permission and start watching
        gpsWatchId = navigator.geolocation.watchPosition(
            handleGPSSuccess,
            handleGPSError,
            {
                enableHighAccuracy: true,  // Use GPS for best accuracy
                maximumAge: 0,              // Don't use cached position
                timeout: 5000               // 5 second timeout
            }
        );

        gpsStatus.textContent = 'Acquiring...';
        gpsStatus.style.color = '#f59e0b';
        gpsButton.textContent = 'GPS Acquiring...';
        gpsButton.disabled = true;

    } catch (error) {
        console.error('Error enabling GPS:', error);
        gpsStatus.textContent = 'Error';
        gpsStatus.style.color = '#ef4444';
    }
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

    switch(error.code) {
        case error.PERMISSION_DENIED:
            errorMsg = 'Permission Denied';
            break;
        case error.POSITION_UNAVAILABLE:
            errorMsg = 'Position Unavailable';
            break;
        case error.TIMEOUT:
            errorMsg = 'Timeout';
            break;
    }

    gpsStatus.textContent = errorMsg;
    gpsStatus.style.color = '#ef4444';
    gpsButton.disabled = false;
    gpsButton.textContent = 'Enable GPS';

    console.error('GPS Error:', error.message);
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
startButton.addEventListener('click', startSensors);
gpsButton.addEventListener('click', enableGPS);

// Initialize on load
window.addEventListener('load', () => {
    checkSensorSupport();
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
