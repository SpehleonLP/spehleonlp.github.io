# Device Sensor Demo

A simple web app that displays real-time sensor data from your device including orientation, acceleration, gyroscope, and GPS velocity readings.

## Features

- **Real-time Sensor Data**: Display orientation, acceleration, and gyroscope data
- **GPS Velocity Tracking**: Get speed and heading from GPS (Doppler effect)
- **GPS Position**: Latitude, longitude, altitude, and accuracy
- **iOS Compatible**: Works with Device Orientation/Motion APIs
- **Responsive Design**: Works on desktop and mobile devices
- **No AR/VR Popups**: Simple sensor reading without immersive mode

## How to Use

### Basic Sensors (Orientation, Acceleration, Gyroscope)

1. Open the page (must be HTTPS for sensor access)
2. Click "Enable Sensors" button
3. Grant sensor permissions when prompted (iOS only)
4. View live sensor data updating in real-time

### GPS Velocity & Position

1. Click "Enable GPS" button
2. Grant location permission when prompted
3. Wait a few seconds for GPS to acquire signal
4. View speed (km/h), heading (degrees), altitude, and position data

**Note**: GPS velocity uses Doppler shift for accurate speed measurement, unlike integrating accelerometer data which accumulates errors.

### iOS Specific

On iOS 13+, you'll need to:
1. Click the "Enable Sensors" button
2. Tap "Allow" when prompted for motion & orientation access
3. Click "Enable GPS" and allow location access
4. If Safari doesn't work, try the [Mozilla WebXR Viewer](https://apps.apple.com/us/app/webxr-viewer/id1295998056)

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
  - Measures device movement acceleration (not velocity)

- **Gyroscope**: Rotation rate (x, y, z) in degrees/s
  - Measures how fast the device is rotating

- **GPS Velocity**: Speed and heading from GPS
  - **Speed**: Velocity in km/h derived from GPS Doppler shift
  - **Heading**: Direction of travel in degrees (0-360, true north)
  - **Altitude**: Height above sea level in meters
  - Note: Shows "N/A" when stationary or if GPS can't determine value

- **GPS Position**: Location data
  - **Latitude**: North-south position in degrees
  - **Longitude**: East-west position in degrees
  - **Accuracy**: Estimated accuracy radius in meters (lower is better)

## Browser Compatibility

| Browser | Platform | Sensors | GPS | Notes |
|---------|----------|---------|-----|-------|
| Chrome/Edge | Desktop | ✅ | ✅ | Requires HTTPS |
| Chrome | Android | ✅ | ✅ | Full support |
| Safari | iOS | ✅ | ✅ | Requires permissions |
| Firefox | All | ✅ | ✅ | Full support |

**GPS Notes**:
- GPS velocity (speed/heading) requires device movement
- Best results outdoors with clear sky view
- May show "N/A" when stationary or indoors
- `enableHighAccuracy: true` uses GPS instead of WiFi/cell tower triangulation

## Troubleshooting

### Sensors Show "-" or Zero

- Ensure you've clicked "Enable Sensors"
- Grant permission when prompted (iOS)
- Check that you're using HTTPS (required on mobile)
- Make sure your device has accelerometer/gyroscope sensors

### GPS Shows "N/A" for Speed/Heading

- **GPS requires movement** to calculate speed and heading
- Go outside for better GPS signal
- Wait 10-30 seconds for GPS to acquire satellites
- Speed/heading may be null when stationary (this is normal)
- Some devices don't provide heading data

### GPS "Position Unavailable"

- Enable location services in device settings
- Allow location access in browser when prompted
- Go outside or near a window
- GPS works poorly indoors

### Permission Denied on iOS

- Go to Settings > Safari > Motion & Orientation Access
- Go to Settings > Privacy > Location Services
- Enable for your website/Safari
- Refresh the page

### Still Not Working?

- Try using Mozilla WebXR Viewer on iOS
- Check browser console for errors
- Ensure you're not blocking sensors/location in browser settings
- Make sure you're using HTTPS

## References

- [Device Orientation Events API](https://developer.mozilla.org/en-US/docs/Web/API/DeviceOrientationEvent)
- [Device Motion Events API](https://developer.mozilla.org/en-US/docs/Web/API/DeviceMotionEvent)
- [Geolocation API](https://developer.mozilla.org/en-US/docs/Web/API/Geolocation_API)
- [GPS Doppler Effect for Velocity](https://en.wikipedia.org/wiki/Doppler_effect#Satellite_navigation)
- [Sensor-JS Research](https://sensor-js.xyz/)
