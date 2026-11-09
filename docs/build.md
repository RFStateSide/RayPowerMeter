# Build and configuration

## Qt desktop

The qualified build uses Windows x64, Qt 6.10.3 with MinGW GCC 13.1, CMake and
Ninja. Install Qt Core, Gui, Widgets, Network, SerialPort and Multimedia.
Use the matching compiler and Qt kit. From the project root:

```sh
cmake -S app-qt -B build/qt -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=path/to/Qt/6.10.3/mingw_64
cmake --build build/qt
```

The build uses the resources named in `app-qt/Resources.qrc`. The approved
versions are listed in the [asset inventory](asset-provenance.md).
The desktop build evidence covers Windows x64. Hamlib is optional and
installed separately for experimental rig-control features.

## ESP32-S3 firmware

Run `pio run -e esp32s3-release` from the project root. The pinned
`esp32s3-devkitc-1` board definition is used with a **16 MiB flash-size
override** and [custom partition CSV](../partitions_raypowermeter_ota_16mb.csv).
Use a compatible ESP32-S3 module with 16 MiB physical flash, support for the image’s DIO
flash mode at 80 MHz, and compatible controller pin assignments. A PlatformIO board banner
can display the base board's 8 MiB description. The image headers and
partition layout establish the configured target.

| Partition | Offset | Size |
| --- | --- | --- |
| NVS | `0x9000` | `0x5000` |
| OTA data | `0xE000` | `0x2000` |
| App 0 | `0x10000` | `0x1E0000` |
| App 1 | `0x1F0000` | `0x1E0000` |
| SPIFFS | `0x3D0000` | `0xC30000` |

The two OTA application slots are retained in the partition format. Firmware
OTA download is disabled in all supplied profiles. The historical controller
schematic names an ESP32-S3-DevKitC-1-N8R2, which has 8 MiB flash and 2 MiB
PSRAM. The fitted modules on historical builds and revisions have not been
verified here. The qualified 16 MiB build is incompatible with that 8 MiB
part. Check the fitted module and partition layout before any programming.

[Espressif module specifications](https://docs.espressif.com/projects/esp-idf/en/v5.3/esp32s3/hw-reference/esp32s3/user-guide-devkitc-1.html)

## Serial and isolated-laboratory networking

Fresh Qt settings select serial. In Setup, choose the device's remote UART
adapter and 115200 baud. The remote interface uses UART2 with line-delimited
JSON. The console and Nextion connections are separate. Existing Qt transport
and host preferences are retained. Switch existing HTTP profiles to serial
when using the standard firmware.

The default release and debug firmware profiles keep Wi-Fi and HTTP inactive
even if older EEPROM contains an enabled Wi-Fi configuration. Saved settings
and credentials remain in EEPROM. For an isolated laboratory network, build
the explicit `esp32s3-lab-network` profile. Fresh EEPROM still requires Wi-Fi
setup. That profile can reuse an older saved, enabled connection on startup.
Review those settings before using the lab profile. Its HTTP API includes
unauthenticated writes and is intended for an isolated network.

## Host checks

Use ArduinoJson 7.4.2 and point CMake at its `src` directory.

```sh
cmake -S tests/calibration -B build/calibration-host -DARDUINOJSON_SRC=.pio/libdeps/esp32s3-release/ArduinoJson/src
cmake --build build/calibration-host
ctest --test-dir build/calibration-host --output-on-failure
```

The firmware was compiled with cached dependencies. A cold-cache attempt
previously stopped at GitHub DNS resolution.
