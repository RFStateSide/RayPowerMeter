# RayPowerMeter

RayPowerMeter is RF StateSide LLC's ESP32-S3 and Qt engineering project for
forward and reflected RF power, SWR, and station instrumentation development.

**Public Engineering Reference — AS-IS.** The proposed source release supports
study, modification and continued development. It carries no claim of certified
accuracy, guaranteed RF power handling or fitness for safety-critical use.

## Source and build

- [Build and configuration](docs/build.md) covers the Windows x64 Qt build, the
  ESP32-S3 16 MiB flash target, serial setup, isolated-lab networking and tests.
- [Calibration](docs/calibration.md) documents the measured defaults, estimated
  region, per-path calibration and saved settings.
- [Known limitations](docs/known-limitations.md) describes measurement,
  communications and control behavior.
- [Licensing](docs/licensing.md) and [asset provenance](docs/asset-provenance.md)
  identify the approved software license, cleared assets and deferred materials.

The source build targets a compatible ESP32-S3 configuration with **16 MiB flash**.
The historical controller drawing names an 8 MiB N8R2 module. Verify the actual
fitted module before selecting a flash layout. The default firmware leaves Wi-Fi,
HTTP and OTA inactive. Fresh desktop settings select serial.

The factory calibration tables are starting points. Users are responsible for
calibrating their own assemblies against suitable references and verifying their
operating limits. The separate [RayCouplers hardware reference](https://github.com/RFStateSide/RayCouplers)
documents the couplers and their historical measurements.

## License and credits

Original RayPowerMeter application and firmware code is licensed under
**LGPL-3.0-only**. See [LICENSE](LICENSE), the incorporated [GPL version 3 text](LICENSES/GPL-3.0.txt),
and [NOTICE](NOTICE). Third-party code and assets remain subject to their own terms.

Original engineering by Ray Faucher (KB1SO), RF StateSide LLC.
ChatGPT/Codex assisted with source review, corrections, test development and
release preparation.
