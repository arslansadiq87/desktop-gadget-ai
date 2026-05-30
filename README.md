# ESP32-S3 2 Inch Display Launcher

PlatformIO firmware for an ESP32-S3 N16R8 board driving a 2 inch ST7789 display. The launcher includes CCTV snapshots, WiFi setup, weather, a clock face, button controls, buzzer feedback, and battery status display.

## Features

- ST7789 240 x 320 display used in landscape mode
- Four-button launcher UI
- CCTV snapshot viewer using HTTPS Digest authentication
- WiFi network scan, numeric password entry, and saved credentials in ESP32 NVS
- Open-Meteo current weather dashboard
- Analog clock screen
- Optional button buzzer sound
- Optional battery voltage monitor

## Hardware

Default pin mapping in `src/main.cpp`:

| Function | GPIO |
| --- | ---: |
| TFT MOSI | 4 |
| TFT SCLK | 3 |
| TFT CS | 7 |
| TFT DC | 6 |
| TFT RST | 5 |
| TFT BL | 2 |
| Button 1-4 | 8, 9, 10, 11 |
| Buzzer | 12 |
| Battery ADC | 1 |

Target board:

- ESP32-S3 DevKitC-1 N16R8 compatible board
- 16 MB flash
- 8 MB PSRAM
- Arduino framework through PlatformIO

## Secrets And Local Configuration

Private values are not committed. The firmware optionally includes `include/secrets.h` when it exists.

1. Copy `include/secrets.example.h` to `include/secrets.h`.
2. Fill in your WiFi and CCTV values.
3. Keep `include/secrets.h` local. It is ignored by git.

```cpp
#define PROJECT_INITIAL_WIFI_SSID "YOUR_WIFI_SSID"
#define PROJECT_INITIAL_WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

#define PROJECT_CCTV_DVR_HOST "YOUR_DVR_HOST_OR_IP"
#define PROJECT_CCTV_DVR_PORT 443
#define PROJECT_CCTV_DVR_USER "YOUR_DVR_USER"
#define PROJECT_CCTV_DVR_PASSWORD "YOUR_DVR_PASSWORD"
```

The project builds without `include/secrets.h`. In that state WiFi can still be configured from the Settings app, and CCTV will show a configuration message until DVR settings are provided.

## Build

Install PlatformIO Core or use the PlatformIO extension for VS Code.

```sh
pio run
```

## Upload

Connect the ESP32-S3 and upload:

```sh
pio run -t upload
```

If PlatformIO does not auto-detect the serial port, pass it at upload time:

```sh
pio run -t upload --upload-port COM16
```

On Linux/macOS, use the detected device path, for example `/dev/ttyACM0` or `/dev/cu.usbmodemXXXX`.

## Serial Monitor

```sh
pio device monitor -b 115200
```

## Using The Device

Main menu:

- `B1 OK`: open the selected app
- `B2 Back`: return to apps from a page
- `B3 Prev`: previous app or option
- `B4 Next`: next app or option

Settings:

- `WiFi`: scan, select, enter numeric password, connect, and save
- `Button Buzzer Sound`: toggle button beep on or off

WiFi password entry:

- `B1 Next`: append the selected digit
- Hold `B1`: connect and save
- `B2 Del`: delete previous digit
- Double `B2`: exit password entry
- `B3 -` / `B4 +`: change digit

CCTV:

- `B1 Shot`: refresh current channel
- `B2 Back`: return to apps
- `B3 Ch-`: previous channel
- `B4 Ch+`: next channel

Weather:

- `B1 Sync`: refresh weather
- `B2 Back`: return to apps

## Weather Location

Weather uses Open-Meteo over HTTP. Defaults are public example coordinates. Override them in `include/secrets.h`:

```cpp
#define PROJECT_WEATHER_CITY "London"
#define PROJECT_WEATHER_LATITUDE 51.5072f
#define PROJECT_WEATHER_LONGITUDE -0.1276f
```

## Battery Reading

Battery ADC is disabled by default:

```cpp
constexpr bool BATTERY_ADC_ENABLED = false;
```

Enable it only after confirming the ADC pin and resistor divider are correct for your board. Tune these values in `src/main.cpp`:

- `BATTERY_ADC_PIN`
- `BATTERY_DIVIDER_RATIO`
- `BATTERY_EMPTY_MV`
- `BATTERY_FULL_MV`

## Repository Hygiene

This repository ignores local secrets, PlatformIO build outputs, generated firmware artifacts, and editor cache files. The committed files needed for a clean build are:

- `src/main.cpp`
- `platformio.ini`
- `partitions_16mb.csv`
- `include/secrets.example.h`
- helper files such as `.gitignore`, `.editorconfig`, `.gitattributes`, and GitHub Actions workflow

## CI

GitHub Actions builds the firmware on push and pull request using PlatformIO. The CI build uses the public defaults and does not need private secrets.
