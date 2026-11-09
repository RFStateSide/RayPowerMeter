# Licensing

RF StateSide LLC approved GNU Lesser General Public License version 3 for its
original RayPowerMeter application and firmware. The SPDX identifier is
`LGPL-3.0-only`. The approved wording names version 3 and supplies no grant
for later versions. [LICENSE](../LICENSE) and [GPL version 3](../LICENSES/GPL-3.0.txt)
contain the unmodified Free Software Foundation texts. [NOTICE](../NOTICE)
records the project scope and attribution. The separate RayCouplers hardware
license does not determine RayPowerMeter software or controller CAD terms.

## Dependencies

| Dependency | Role and terms |
| --- | --- |
| Qt 6.10.3 | External Qt Core, Gui, Widgets, Network, SerialPort and Multimedia. The Qt installation governs its own LGPL/GPL or commercial terms and bundled third-party notices. [Qt component notices](https://doc.qt.io/qt-6.10/licenses-used-in-qt.html) |
| Hamlib | Optional external `rigctld` process. Hamlib libraries and utilities have their own LGPL and GPL terms. No Hamlib runtime is included. [Hamlib notices](https://github.com/Hamlib/Hamlib/blob/master/LICENSE) |
| pioarduino 55.03.35 | External PlatformIO platform, Apache-2.0. [Pinned platform](https://github.com/pioarduino/platform-espressif32/tree/55.03.35) |
| Arduino-ESP32 3.3.5 | External framework, LGPL-2.1-or-later, with separately licensed components in the SDK and toolchain. [Framework source](https://github.com/espressif/arduino-esp32/tree/3.3.5) |
| ArduinoJson 7.4.2 | MIT. Used by firmware and the host fixture. [License](https://github.com/bblanchon/ArduinoJson/blob/v7.4.2/LICENSE.txt) |
| AsyncTCP 3.4.9 and ESPAsyncWebServer 3.9.3 | LGPL-3.0 family. Retain their upstream notices and SPDX terms for any redistributed copies. [AsyncTCP](https://github.com/ESP32Async/AsyncTCP/tree/v3.4.9) · [ESPAsyncWebServer](https://github.com/ESP32Async/ESPAsyncWebServer/tree/v3.9.3) |

The proposed snapshot supplies build inputs and dependency references. A
later distribution of binaries, vendored or modified dependencies requires
its own exact dependency manifest, applicable notices and source or relinking
arrangements where required by the applicable licenses.

## Earlier RF StateSide projects

Ray Faucher (KB1SO) confirmed that MercuryLuxPc, RayEltekController and
RayVessel are his original engineering projects. He explicitly authorized
reuse of the identified original contributions from all three under
LGPL-3.0-only in this RayPowerMeter release. RF StateSide LLC is the publisher.

- `app-qt/src/app/dialogviewlogfile.cpp` and `app-qt/src/app/dialogviewlogfile.h`
  came from MercuryLuxPc and contain earlier RayVessel code. The notices
  credit Ray Faucher and RF StateSide LLC for the original RayVessel work
  and Ray Faucher for the MercuryLuxPc work. The development history retains
  the earlier AGPL notices
  and MercuryLuxPc's later licensing language.
- `firmware/app/net/NetManager.cpp` and `firmware/app/net/NetManager.h` derive
  from RayEltekController. `firmware/app/ui/CustomCommands.h` adapts its
  command pattern. `firmware/app/ui/RayPowerMeterUiController.h` identifies
  copied component names inline. The existing author and lineage comments
  are retained.

The focused predecessor comparison identified ordinary Qt and Arduino
interfaces and no identifiable embedded third-party implementation in the
six files. Dependencies remain subject to the terms listed above. The five
[Qt graphics](asset-provenance.md) are separately cleared assets outside the
software SPDX grant.

[RF StateSide project history](https://rfstateside.com/projects) ·
[SPDX license identifier](https://spdx.org/licenses/LGPL-3.0-only.html)
