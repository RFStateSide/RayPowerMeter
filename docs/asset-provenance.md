# Asset provenance and snapshot inventory

The [183-path public-source allowlist](public-source-allowlist.txt) defines
the proposed source snapshot. It includes the Qt and firmware source, tests,
build files, partition CSV, reviewed Markdown and the required Qt resources.
The six earlier-project source files are recorded in [Licensing](licensing.md).

## Qt resources

The owner confirms that Ray Faucher and ChatGPT created all five graphics
collaboratively for RayPowerMeter and that they are original project assets.
RF StateSide LLC is authorized to distribute the listed versions in the
source package. The graphics clearance is separate from the software license.
Credit Ray Faucher and ChatGPT assistance in the project notices.

| Path under `app-qt/assets/icons/` | Use | Development evidence |
| --- | --- | --- |
| `raypowermeter.ico` | Application icon | Added in `2a47350`. Its artwork closely matches the initial About PNG from that commit, which carries ChatGPT generation markers. |
| `RfssLogo1024_659.png` | Export watermark for light backgrounds | RF StateSide wordmark and tower. Added in `2a47350`. |
| `RfssLogo1024_659_white.png` | Export watermark for dark backgrounds | Light-lettering variant. Added in `2a47350`. |
| `Splash.png` | Startup splash | RayPowerMeter wordmark, tower and meter composite. Added in `48d91f3`. |
| `icon1024.png` | About dialog | Tower and meter artwork. The current version replaced the initial PNG in `df1105c`. |

The focused inspection identified no embedded stock artwork or third-party
marks in these five files. Their existing versions are retained. The
[resource manifest](../app-qt/Resources.qrc) also includes the project
default layout. The Nextion alarm artwork and generated glyphs are outside
this clearance.

## Deferred or excluded

| Files | Reason |
| --- | --- |
| `nextion/**` | The HMI contains the exact Icons8 alarm PNG and references to Arial, Calibri and DS-Digital. The loose image and generated glyph resources have unresolved redistribution terms. Hold HMI, pages, artwork and glyphs together. A future HMI review needs exact asset permissions and attribution, or cleared replacements. [Icons8 terms](https://icons8.com/license) · [Microsoft font terms](https://learn.microsoft.com/en-us/typography/fonts/font-faq) |
| `hardware/controller/**` | Historical KiCad design, imported parts including SnapEDA metadata, custom libraries and private model paths. Verify hardware ownership and imported assets before choosing hardware terms. The 8 MiB schematic designation is distinct from the 16 MiB firmware configuration. [KiCad terms](https://www.kicad.org/libraries/license/) · [SnapMagic terms](https://www.snapeda.com/about/terms/) |
| `docs/**/*.docx` and historical `docs/api/*.md` | Historical evidence and versioned drafts, including author metadata and older API contracts. Use the new reviewed Markdown guides for the first release. |
| Other `app-qt/assets/icons/*` | Unused variants for the current build. Retain them in development. |
| `.pio/`, `build*/`, `.vscode/`, editor/KiCad local state, logs, archives and private evidence | Local build, configuration or investigation material. Credentials and user settings stay local. |
| `releases/`, installers, EXE, firmware BIN/ELF and TFT | Initial release is source only. No binaries are qualified for publication. |

The private development repository remains the engineering archive. The
future public repository should use an approved snapshot and a provenance
reference to the accepted development baseline.
