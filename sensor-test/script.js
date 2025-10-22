// Initialize WebXR Polyfill for iOS compatibility
if (typeof WebXRPolyfill !== 'undefined') {
    const polyfill = new WebXRPolyfill();
    console.log('WebXR Polyfill initialized');
}

// Global variables
let xrSession = null;
let xrRefSpace = null;
let gl = null;
let canvas = null;

// Sensor data storage
const sensorData = {
    orientation: { alpha: 0, beta: 0, gamma: 0 },
    acceleration: { x: 0, y: 0, z: 0 },
    gyroscope: { x: 0, y: 0, z: 0 },
    position: { x: 0, y: 0, z: 0 }
};

// DOM elements
const startARButton = document.getElementById('start-ar');
const startVRButton = document.getElementById('start-vr');
const stopButton = document.getElementById('stop-session');
const webxrStatus = document.getElementById('webxr-status');
const sessionStatus = document.getElementById('session-status');

// Check WebXR support
async function checkWebXRSupport() {
    if (navigator.xr) {
        const arSupported = await navigator.xr.isSessionSupported('immersive-ar');
        const vrSupported = await navigator.xr.isSessionSupported('immersive-vr');

        if (arSupported || vrSupported) {
            webxrStatus.textContent = 'Supported ✓';
            webxrStatus.style.color = '#10b981';
            startARButton.disabled = !arSupported;
            startVRButton.disabled = !vrSupported;
        } else {
            webxrStatus.textContent = 'Not Supported ✗';
            webxrStatus.style.color = '#ef4444';
        }
    } else {
        webxrStatus.textContent = 'Not Available ✗';
        webxrStatus.style.color = '#ef4444';
        startARButton.disabled = true;
        startVRButton.disabled = true;
    }
}

// Initialize canvas and WebGL
function initGL() {
    canvas = document.getElementById('xr-canvas');
    gl = canvas.getContext('webgl', { xrCompatible: true });

    if (!gl) {
        console.error('WebGL not supported');
        return false;
    }

    return true;
}

// Start XR session
async function startXRSession(mode) {
    if (!gl && !initGL()) {
        alert('WebGL initialization failed');
        return;
    }

    try {
        // Request appropriate session
        xrSession = await navigator.xr.requestSession(mode, {
            requiredFeatures: ['local-floor'],
            optionalFeatures: ['hand-tracking', 'local', 'bounded-floor']
        });

        // Set up session
        await setupXRSession();

        sessionStatus.textContent = `${mode.toUpperCase()} Active`;
        sessionStatus.style.color = '#10b981';
        startARButton.disabled = true;
        startVRButton.disabled = true;
        stopButton.disabled = false;

        // Start device orientation tracking (fallback for additional data)
        startOrientationTracking();

    } catch (error) {
        console.error('Failed to start XR session:', error);
        alert('Failed to start XR session. Make sure you\'re using HTTPS and have granted necessary permissions.');
        sessionStatus.textContent = 'Failed to Start';
        sessionStatus.style.color = '#ef4444';
    }
}

// Setup XR session
async function setupXRSession() {
    xrSession.updateRenderState({
        baseLayer: new XRWebGLLayer(xrSession, gl)
    });

    // Get reference space
    xrRefSpace = await xrSession.requestReferenceSpace('local-floor');

    // Handle session end
    xrSession.addEventListener('end', onSessionEnd);

    // Start render loop
    xrSession.requestAnimationFrame(onXRFrame);
}

// XR animation frame callback
function onXRFrame(time, frame) {
    const session = frame.session;
    session.requestAnimationFrame(onXRFrame);

    // Get viewer pose
    const pose = frame.getViewerPose(xrRefSpace);

    if (pose) {
        const transform = pose.transform;
        const position = transform.position;
        const orientation = transform.orientation;

        // Update position data
        sensorData.position.x = position.x;
        sensorData.position.y = position.y;
        sensorData.position.z = position.z;

        // Convert quaternion to euler angles for orientation
        const euler = quaternionToEuler(orientation);
        sensorData.orientation.alpha = euler.yaw;
        sensorData.orientation.beta = euler.pitch;
        sensorData.orientation.gamma = euler.roll;

        // Update UI
        updateSensorDisplay();

        // Render (basic clear for now)
        const layer = session.renderState.baseLayer;
        gl.bindFramebuffer(gl.FRAMEBUFFER, layer.framebuffer);
        gl.clearColor(0.1, 0.1, 0.1, 1.0);
        gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
    }
}

// Convert quaternion to euler angles
function quaternionToEuler(q) {
    const { x, y, z, w } = q;

    // Roll (x-axis rotation)
    const sinr_cosp = 2 * (w * x + y * z);
    const cosr_cosp = 1 - 2 * (x * x + y * y);
    const roll = Math.atan2(sinr_cosp, cosr_cosp);

    // Pitch (y-axis rotation)
    const sinp = 2 * (w * y - z * x);
    const pitch = Math.abs(sinp) >= 1 ? Math.sign(sinp) * Math.PI / 2 : Math.asin(sinp);

    // Yaw (z-axis rotation)
    const siny_cosp = 2 * (w * z + x * y);
    const cosy_cosp = 1 - 2 * (y * y + z * z);
    const yaw = Math.atan2(siny_cosp, cosy_cosp);

    return {
        roll: roll * 180 / Math.PI,
        pitch: pitch * 180 / Math.PI,
        yaw: yaw * 180 / Math.PI
    };
}

// Start device orientation tracking (fallback/additional data)
function startOrientationTracking() {
    // Request permission for iOS 13+
    if (typeof DeviceOrientationEvent !== 'undefined' &&
        typeof DeviceOrientationEvent.requestPermission === 'function') {
        DeviceOrientationEvent.requestPermission()
            .then(response => {
                if (response === 'granted') {
                    addOrientationListeners();
                }
            })
            .catch(console.error);
    } else {
        addOrientationListeners();
    }
}

// Add orientation event listeners
function addOrientationListeners() {
    window.addEventListener('deviceorientation', handleOrientation);
    window.addEventListener('devicemotion', handleMotion);
}

// Handle device orientation
function handleOrientation(event) {
    if (!xrSession) {
        sensorData.orientation.alpha = event.alpha || 0;
        sensorData.orientation.beta = event.beta || 0;
        sensorData.orientation.gamma = event.gamma || 0;
        updateSensorDisplay();
    }
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

    if (!xrSession) {
        updateSensorDisplay();
    }
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

    // Position
    updateElement('pos-x', sensorData.position.x);
    updateElement('pos-y', sensorData.position.y);
    updateElement('pos-z', sensorData.position.z);
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

// End session
function onSessionEnd() {
    xrSession = null;
    sessionStatus.textContent = 'Ended';
    sessionStatus.style.color = '#6b7280';
    startARButton.disabled = false;
    startVRButton.disabled = false;
    stopButton.disabled = true;

    // Remove event listeners
    window.removeEventListener('deviceorientation', handleOrientation);
    window.removeEventListener('devicemotion', handleMotion);
}

// Stop XR session
async function stopXRSession() {
    if (xrSession) {
        await xrSession.end();
    }
}

// Event listeners
startARButton.addEventListener('click', () => startXRSession('immersive-ar'));
startVRButton.addEventListener('click', () => startXRSession('immersive-vr'));
stopButton.addEventListener('click', stopXRSession);

// Initialize on load
window.addEventListener('load', () => {
    checkWebXRSupport();
    initGL();

    // Start basic orientation tracking even without XR session
    if (window.location.protocol === 'https:' || window.location.hostname === 'localhost') {
        startOrientationTracking();
    } else {
        console.warn('HTTPS required for sensor access');
    }
});

// Handle visibility change
document.addEventListener('visibilitychange', () => {
    if (document.hidden && xrSession) {
        stopXRSession();
    }
});
