// Sensor data storage
const sensorData = {
    orientation: { alpha: 0, beta: 0, gamma: 0 },
    acceleration: { x: 0, y: 0, z: 0 },
    gyroscope: { x: 0, y: 0, z: 0 }
};

// DOM elements
const startButton = document.getElementById('start-sensors');
const sensorStatus = document.getElementById('sensor-status');

let sensorsActive = false;

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

// Update individual element
function updateElement(id, value) {
    const element = document.getElementById(id);
    if (element) {
        element.textContent = typeof value === 'number' ? value.toFixed(2) : value;
        element.classList.add('updating');
        setTimeout(() => element.classList.remove('updating'), 500);
    }
}

// Event listeners
startButton.addEventListener('click', startSensors);

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
