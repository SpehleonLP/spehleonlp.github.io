# Device Sensor Demo

A simple web app that displays real-time sensor data from your device including orientation, acceleration, and gyroscope readings.

## Features

- **Real-time Sensor Data**: Display orientation, acceleration, and gyroscope data
- **iOS Compatible**: Works with Device Orientation/Motion APIs
- **Responsive Design**: Works on desktop and mobile devices
- **No AR/VR Popups**: Simple sensor reading without immersive mode

## How to Use

### On Any Device

1. Open the page (must be HTTPS for sensor access)
2. Click "Enable Sensors" button
3. Grant sensor permissions when prompted (iOS only)
4. View live sensor data updating in real-time

### iOS Specific

On iOS 13+, you'll need to:
1. Click the "Enable Sensors" button
2. Tap "Allow" when prompted for motion & orientation access
3. If Safari doesn't work, try the [Mozilla WebXR Viewer](https://apps.apple.com/us/app/webxr-viewer/id1295998056)

## Running Locally

### Simple HTTP Server

```bash
# Python 3
python3 -m http.server 8000

# Python 2
python -m SimpleHTTPServer 8000
```

### HTTPS Required for Production

For sensor access to work on mobile devices, you need HTTPS. Options:

1. **Deploy to GitHub Pages** (automatic HTTPS)
2. **Use ngrok for testing:**
   ```bash
   python3 -m http.server 8000
   # In another terminal:
   ngrok http 8000
   ```
3. **Local HTTPS server:**
   ```bash
   npm install -g http-server
   http-server -S -C cert.pem -K key.pem -p 8000
   ```

## Sensor Data Explained

- **Orientation**: Device rotation (alpha, beta, gamma) in degrees
  - Alpha: Rotation around Z-axis (0-360°)
  - Beta: Rotation around X-axis (-180 to 180°)
  - Gamma: Rotation around Y-axis (-90 to 90°)

- **Acceleration**: Linear acceleration (x, y, z) in m/s²
  - Measures device movement speed

- **Gyroscope**: Rotation rate (x, y, z) in degrees/s
  - Measures how fast the device is rotating

## Browser Compatibility

| Browser | Platform | Support | Notes |
|---------|----------|---------|-------|
| Chrome/Edge | Desktop | ✅ | May need HTTPS |
| Chrome | Android | ✅ | Full support |
| Safari | iOS | ✅ | Requires permission prompt |
| Firefox | All | ✅ | Full support |

## Troubleshooting

### Sensors Show "-" or Zero

- Ensure you've clicked "Enable Sensors"
- Grant permission when prompted (iOS)
- Check that you're using HTTPS (required on mobile)
- Make sure your device has accelerometer/gyroscope sensors

### Permission Denied on iOS

- Go to Settings > Safari > Motion & Orientation Access
- Enable for your website
- Refresh the page

### Still Not Working?

- Try using Mozilla WebXR Viewer on iOS
- Check browser console for errors
- Ensure you're not blocking motion sensors in browser settings

## References

- [Device Orientation Events API](https://developer.mozilla.org/en-US/docs/Web/API/DeviceOrientationEvent)
- [Device Motion Events API](https://developer.mozilla.org/en-US/docs/Web/API/DeviceMotionEvent)
- [Sensor-JS Research](https://sensor-js.xyz/)
