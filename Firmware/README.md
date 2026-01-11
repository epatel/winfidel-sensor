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


[<- Go back to repository root](../README.md)
