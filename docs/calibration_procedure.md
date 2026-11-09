# Calibration and validation procedure

This procedure is a blank protocol, not evidence that any tests have been
performed. Use only **de-energized, current-limited, low-voltage cable
samples**. Never connect the prototype to mains or an energized cable.

## 1. Record the setup before applying power

Record the following in a dated test log. Leave unknown fields blank rather
than estimating them.

- Board revision and firmware source revision.
- Complete wiring diagram made from the actual assembled connections, including
  source, load, cable conductors, INA219 terminals, common ground, and test
  points. Verify each connection electrically.
- INA219 module identity and measured/marked shunt resistance, with method and
  uncertainty.
- LCD backpack identity, measured I²C addresses, expander bit mapping, and
  pull-up arrangement/voltage.
- Cable manufacturer/type, conductor material, conductor area, number of
  conductors, sample length, condition, and known fault location if applicable.
- Test source voltage and current limit, plus actual voltage measured at the
  defined test points.
- Reference meter model, calibration status, range, resolution, and specified
  uncertainty.
- Ambient and cable sample temperature.

Inspect the sample and circuit while de-energized. Confirm current limiting and
ensure no hazardous voltage can be present. Do not assume the image in this
repository establishes wiring or component ratings.

## 2. Verify INA219 calibration

Measure or verify the actual shunt resistance `R_SHUNT` before setting source
configuration. Select the current scale based on the safe current range and
sensor range. Then calculate:

```text
Current_LSB = selected current scale in A/bit
CAL = trunc(0.04096 / (Current_LSB × R_SHUNT))
```

Set `INA219_SHUNT_UOHM`, `INA219_CURRENT_LSB_UA`, and `INA219_CAL_VALUE` in the
firmware user configuration consistently. Confirm the selected INA219 shunt
range can accommodate the expected voltage without overrange. Compare zero
and several nonzero current readings with a suitable reference meter, at the
same defined point and polarity. Record signed raw values, calculated values,
reference values, repeat count, and uncertainty. Do not infer sensor accuracy
from the register resolution.

## 3. Establish current thresholds

The source currently starts with provisional 2 mA low-current and 3 mA
recovery thresholds. These are configuration defaults, not measured or
validated limits. Choose any revised thresholds only from repeated tests on
the intended safe test topology. Record source/load/sensor-disconnected
conditions separately; a low-current indication does not distinguish an open
cable from those conditions.

## 4. Cable resistance-per-length calibration

Only attempt this after the test topology is designed and verified to measure
voltage and current across the same defined cable section. Use known sample
lengths from the same cable type and conductor area. For each sample:

1. Measure the voltage drop across the defined cable section with a reference
   meter and measure current through that same path.
2. Record test current, voltage, ambient/sample temperature, conductor details,
   sample length, reference meter, and repeat data.
3. Determine the section resistance from `R = V / I`.
4. Measure or separately characterize shunt, lead, and contact resistance;
   state how those contributions are compensated.
5. Estimate cable resistance per length only from the documented sample
   measurements. Repeat across the intended temperature and current range.
6. Preserve raw data and report uncertainty and limitations. Do not fit or
   publish a model until repeatability and the intended cable population are
   evaluated.

For a distance model, the resistance must represent a known segment and the
fault path must be defined. A two-wire loop, conductor-to-conductor path, and
single-conductor path have different resistance models. Current-only readings,
or source voltage used in place of cable-segment voltage, are not a valid
distance measurement.

## 5. Validation matrix

Document expected behavior and physical conditions before testing. Exercise
only safe, de-energized cable samples and current-limited low-voltage sources.
Collect repeated normal and low-current observations; separately test sensor
disconnection, source disconnection, and invalid/overrange readings so they
are not mistaken for cable faults. Add fault classes only after the hardware
topology supports them. Do not claim physical localization until tested at
known positions against a reference and uncertainty is reported.

Enter actual observations in
[`validation_log_template.csv`](validation_log_template.csv); it contains
headers only and intentionally has no fabricated rows.
