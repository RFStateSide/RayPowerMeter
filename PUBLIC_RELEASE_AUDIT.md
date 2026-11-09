# RayPowerMeter public release audit

**Candidate for owner review. Publication on hold.** This preparation follows
RF StateSide Hardware/Software Public-Release Workflow v4. Development history
remains with `RayJr2/RayPowerMeter` at baseline
`f56eb30b17429b8f9de191bb96ec69a83b854c78` plus reviewed working-tree changes.
The intended destination is a separate, owner-approved RF StateSide source
snapshot.

| Gate | Disposition |
| --- | --- |
| Measured calibration | 19 measured 200 W and 14 measured 2 kW pairs preserved. The 2,500 W endpoint is an estimate. The prior experimental reflected adjustment is excluded from shipping source and preserved privately. [Calibration](docs/calibration.md) |
| Software license | Owner approved LGPL-3.0-only for original Qt and firmware contributions. Root license texts and 149 C++ source/header notices reconcile the prior conflicting notices. The owner explicitly authorized reuse from MercuryLuxPc, RayEltekController and RayVessel. The six lineage files retain earlier copyright and authorship, including Ray Faucher and RF StateSide LLC for the RayVessel log dialog. [Licensing](docs/licensing.md) |
| Release defaults | Standard firmware gates Wi-Fi and HTTP independent of old EEPROM state. The explicit lab build retains the experimental interface. Supplied profiles exclude historical OTA. Fresh Qt settings select serial. [Build and configuration](docs/build.md) |
| Flash target | Intentional 16 MiB build with dual OTA slots retained. Historical 8 MiB N8R2 documentation is identified as incompatible with this flash layout. Fitted historical modules have not been verified. |
| Publication inventory | Source-only scope. The owner cleared the five required ChatGPT-assisted Qt graphics. The 183-path allowlist includes those resources and the six lineage files. Defer Nextion, CAD, historical documents and binaries. [Asset provenance](docs/asset-provenance.md) |
| Verification | The Windows x64 Qt build, cached ESP32-S3 release build and all three host tests passed from a fresh isolated copy of exactly the 183 allowlisted paths after the owner explicitly confirmed provenance. Dependency caches and generated outputs were outside the source copy. Both image headers report 16 MiB, and the generated partition table has two 1,966,080-byte app slots. The linked default firmware retains the serial server and omits HTTP route registration and the legacy OTA task/downloader. The previous cold-cache attempt stopped at GitHub DNS resolution. Physical performance, integrated device behavior, Nextion and relay/protection operation remain unverified. |

Owner review order is licensing and lineage, graphics and inventory, then
build, calibration and limitations. Final document and publication acceptance
remain open. Repository creation and visibility require separate authorization.
