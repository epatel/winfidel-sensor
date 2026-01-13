# How to build firmware

For our firmware development, we are going to use Arduino. Mainly because it's user friendly, easy to use and
most hobbyists are familiar with it or have already used it. While we could build the same or even more efficient
firmware using bare-metal C and ESP-IDF, we are going to stick with Arduino and hope that this allows the project to be
more friendly and easier to use/modify by wider DIY/hacker community.

## How can I build/compile firmware?

### 1. Install PlatformIO
Installing PlatformIO CLI is pretty straight-forward and also well documented for Windows, Linux and MacOS.
You will need to follow few steps and get PlatformIO CLI installed, detailed tutorial can be found at https://platformio.org/install/cli
Make sure to install [PlatformIO Core](https://docs.platformio.org/en/latest//core/installation.html#installation-methods 'https://docs.platformio.org/en/latest//core/installation.html#installation-methods') and allso that it is available trough [shell](https://docs.platformio.org/en/latest//core/installation.html#piocore-install-shell-commands 'PlatformIO Core - Install Shell Commands¶').

### 2. Install Python dependencies
The build process uses Python scripts to minify web assets. Install the required packages into PlatformIO's virtual environment:

```bash
~/.platformio/penv/bin/pip install minify_html rjsmin
```

### 3. Build firmware
Open shell/command-prompt and navigate to the `Firmware` folder.

Compile and upload the firmware with:
```bash
pio run --target upload
```

The web assets (from `data_pre` folder) are automatically minified and embedded into the firmware during the build process.

### 4. OTA Updates
Once the device is connected to WiFi, you can update the firmware over-the-air using ArduinoOTA.

> **Note:** OTA requires the `min_spiffs.csv` partition table (configured in `platformio.ini`) which provides two app partitions for safe firmware switching.

Using PlatformIO:

```bash
pio run --target upload --environment esp32_ota
```

Or using espota.py directly:
```bash
pio run && ~/.platformio/packages/framework-arduinoespressif32/tools/espota.py \
  -i winfidel.local -p 3232 -f .pio/build/esp32c3/firmware.bin
```

The device will automatically reboot with the new firmware.

## MQTT Support

WInFiDEL supports MQTT for publishing diameter measurements to a broker, enabling integration with home automation systems like Home Assistant, Node-RED, or custom data logging solutions.

### Configuration

Configure MQTT via the web interface at `http://winfidel.local/settings.html`:

- **Enable MQTT**: Toggle MQTT on/off
- **Broker Host**: Your MQTT broker address (e.g., `mqtt.example.com`)
- **Broker Port**: Default is `1883`
- **Username/Password**: Optional authentication credentials
- **Device ID**: Identifier used in topic names (default: `winfidel`)
- **Publish Threshold**: Minimum diameter change (in mm) to trigger a publish (default: `0.01`)

### MQTT Topics

| Topic | Description |
|-------|-------------|
| `winfidel/{device_id}/diameter` | Current measurement data (JSON, retained) |
| `winfidel/{device_id}/status` | Online/offline status (LWT, retained) |
| `winfidel/{device_id}/cmd/reset` | Send any message to reset statistics |
| `winfidel/{device_id}/cmd/calibrate` | Create calibration point (JSON) |

### Payload Formats

**Diameter measurement** (`winfidel/{device_id}/diameter`):
```json
{
  "diameter": 1.75,
  "adc": 2048,
  "min": 1.72,
  "max": 1.78,
  "avg": 1.75,
  "count": 1234
}
```

**Calibration command** (`winfidel/{device_id}/cmd/calibrate`):
```json
{"mm": 1.75}
```
Or with explicit ADC value:
```json
{"mm": 1.75, "adc": 2048}
```

### Testing

Subscribe to all WInFiDEL topics:
```bash
mosquitto_sub -h <broker> -t "winfidel/#" -v
```

### REST API

MQTT settings can also be configured via REST API:

- `GET /api/v0/mqtt/config` - Read current settings
- `POST /api/v0/mqtt/config` - Update settings
- `GET /api/v0/mqtt/status` - Get connection status

## LED Indicators

The green LED provides visual feedback:

| Pattern | Meaning |
|---------|---------|
| Dim brief flash | Normal measurement (every 200ms) |
| Bright longer flash | MQTT message published |

The LED uses PWM for dimming, making it easy to distinguish between routine measurements and actual MQTT activity.


[<- Go back to repository root](../README.md)
