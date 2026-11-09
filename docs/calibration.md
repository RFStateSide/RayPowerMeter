# Calibration

The shipped tables use [historical RayCouplers forward measurements](https://github.com/RFStateSide/RayCouplers/blob/main/calibration/profile-traceability.md).
The 200 W profile has 19 measured pairs. The 2 kW profile has 14 measured pairs
through 1,500 W at 2.175 V. The 2,500 W endpoint at 2.820 V is an estimate.
Results above 1,500 W therefore depend on an estimated interval.

Both detector paths start with the same unscaled reference for the selected
coupler type. This is a factory default, with independently measured reflected
accuracy unverified. The experimental reflected voltage adjustment is absent
from the source baseline. Published coupler power labels identify the hardware
variants and do not establish calibration or safe power handling.

In Hardware Configuration select the coupler and forward or reflected path.
Capture or enter reference power in watts against the detector voltage.
A table accepts 2–20 strictly increasing voltage points in 0–3.3 V and finite,
nonnegative watts. A two-point fit replaces the selected table. The table
interpolates between points, returns zero below the first point, and holds the
last watt value at and above the last point. Live SWR displays 1.00 at or below
0.25 W forward and caps at 99. See [known limitations](known-limitations.md).

Use an appropriate independent reference for the specific assembly, detector
path, band and operating range. Save/export any user points before reset or
firmware changes. A path-specific firmware reset leaves the other path alone.
A type change refreshes factory data and preserves user calibration on that
input. Review the retained user calibration after changing hardware.
Compatible v3 preferences are retained. Earlier EEPROM layouts require a
controlled backup/export before upgrade.
