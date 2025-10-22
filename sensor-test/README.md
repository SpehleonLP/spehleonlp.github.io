# WebXR Sensor Demo

A WebXR-enabled website that demonstrates real-time access to device sensors including orientation, acceleration, gyroscope, and position tracking.

## Features

- **WebXR Support**: Full AR and VR session support
- **Real-time Sensor Data**: Display orientation, acceleration, gyroscope, and position data
- **iOS Compatibility**: Works with Mozilla's WebXR Viewer for iOS devices
- **Fallback Support**: Uses Device Orientation/Motion APIs when WebXR is unavailable
- **Responsive Design**: Works on desktop and mobile devices

## iOS Setup (iPhone/iPad)

Since Safari on iOS doesn't natively support WebXR yet, you'll need to use Mozilla's WebXR Viewer:

1. **Install WebXR Viewer**:
   - Download from the App Store: [WebXR Viewer](https://apps.apple.com/us/app/webxr-viewer/id1295998056)

2. **Open the App**:
   - Launch WebXR Viewer on your iOS device
   - Navigate to your website URL (must be HTTPS)

3. **Grant Permissions**:
   - Allow camera access when prompted (for AR)
   - Allow motion & orientation access when prompted

## Desktop/Android Setup

1. **Chrome/Edge (Windows, Mac, Linux, Android)**:
   - Ensure you're using a recent version (Chrome 79+)
   - Navigate to your site (HTTPS required)
   - Click "Start AR" or "Start VR"

2. **Firefox Reality (VR Headsets)**:
   - Open the browser on your VR device
   - Navigate to your site
   - Enter VR mode

## Running Locally

### Option 1: Python Server (HTTPS)

```bash
# Generate self-signed certificate (one-time setup)
openssl req -x509 -newkey rsa:4096 -keyout key.pem -out cert.pem -days 365 -nodes

# Start HTTPS server
python3 -m http.server 8000 --bind 0.0.0.0
# Then use ngrok or similar for HTTPS access
```

### Option 2: Node.js with HTTPS

```bash
npm install -g http-server
http-server -S -C cert.pem -K key.pem -p 8000
```

### Option 3: Use ngrok for HTTPS

```bash
# Start a local server
python3 -m http.server 8000

# In another terminal
ngrok http 8000
# Use the HTTPS URL provided by ngrok
```

## Technology Stack

- **WebXR Device API**: For immersive AR/VR experiences
- **WebXR Polyfill**: Provides compatibility layer for browsers without native WebXR
- **Device Orientation/Motion APIs**: Fallback sensor access
- **WebGL**: Rendering context for XR sessions

## Browser Compatibility

| Browser | Platform | AR | VR | Notes |
|---------|----------|----|----|-------|
| Chrome/Edge | Desktop | ⚠️ | ✅ | AR requires ARCore-compatible device |
| Chrome | Android | ✅ | ✅ | Requires ARCore |
| Safari | iOS | ❌ | ❌ | Use WebXR Viewer instead |
| WebXR Viewer | iOS | ✅ | ❌ | Mozilla's iOS app |
| Firefox Reality | VR Headsets | - | ✅ | VR headsets only |

## Sensor Data Explained

- **Orientation**: Device rotation (alpha, beta, gamma) in degrees
- **Acceleration**: Linear acceleration (x, y, z) in m/s²
- **Gyroscope**: Rotation rate (x, y, z) in degrees/s
- **Position**: 3D position (x, y, z) in meters (WebXR only)

## Security Notes

- HTTPS is **required** for sensor and camera access
- Users must grant permission for camera (AR) and sensors
- Motion sensors may require explicit permission on iOS 13+

## Troubleshooting

### "WebXR Not Supported" Message

- Ensure you're using HTTPS (not HTTP)
- Update your browser to the latest version
- On iOS, use WebXR Viewer app instead of Safari
- Check if your device supports ARCore (Android) or ARKit (iOS)

### Sensor Data Shows "-" or Zeros

- Grant sensor permissions when prompted
- Ensure device has motion sensors (accelerometer, gyroscope)
- Try starting an XR session first
- On iOS, ensure you've granted motion & orientation access

### AR Mode Not Available

- Verify your device supports AR (ARCore on Android, ARKit on iOS)
- On iOS, must use WebXR Viewer app
- Camera permission must be granted

## References

- [WebXR Device API Specification](https://immersive-web.github.io/webxr/)
- [Mozilla WebXR Viewer](https://github.com/mozilla-mobile/webxr-ios)
- [WebXR Polyfill](https://github.com/immersive-web/webxr-polyfill)
- [Device Orientation Events](https://developer.mozilla.org/en-US/docs/Web/API/DeviceOrientationEvent)
