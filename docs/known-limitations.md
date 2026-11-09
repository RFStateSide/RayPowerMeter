# Known limitations

RayPowerMeter is a Public Engineering Reference — AS-IS. Historical project
notes report operation of earlier assemblies and serial/HTTP workflows.
Current host calculations and builds verify software behavior within their
test scope. Physical RF accuracy, device integration, Nextion operation,
protection behavior and safe hardware power limits remain unverified for
the prepared baseline.

- **Measurement defaults.** Coupler, detector and ADC behavior vary by
  assembly and operating conditions. Calibrate each instrument against a
  suitable independent reference. Table saturation and the low-forward SWR
  convention can conceal an out-of-range or unusable measurement.
- **Stale readings.** Qt shows connection and update age. Numeric widgets
  may continue displaying the last values after a communication loss.
- **Modulation capture.** Live power ADC sampling can pause during capture.
  The protection loop can continue to see older values. The modulation
  scope's RF-equivalent voltage uses a separate linear estimate.
- **Network writes.** Standard firmware keeps Wi-Fi and HTTP inactive.
  The isolated-lab build exposes unauthenticated HTTP configuration and
  calibration writes. Keep it on an isolated network.
- **OTA.** Supplied builds exclude the historical firmware download task.
  The retained source path skipped TLS certificate validation. OTA support
  requires a separately qualified validation path.
- **Rig control.** Experimental Hamlib features can issue PTT commands.
  Automated transmission and loss-of-control behavior remain unqualified.
- **Relay and protection.** Thresholds and relay behavior are experimental.
  They are unsuitable as the sole protection mechanism or for safety-critical
  use. Independently verify any implementation before relying on it.
