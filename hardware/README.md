# Hardware notes

The supplied photograph is an appearance record, not a wiring reference. It is
not sufficiently clear to confirm all module identities, power connections,
pull-up values, or pin-to-pin paths. No electrical schematic or verified
wiring diagram was supplied, so none is asserted here.

## Target and configured interface

- Target MCU: STM32F401CCU6 Black Pill.
- Current/voltage monitor: INA219; source assumes 7-bit address `0x40`.
- Display: 16×2 I²C LCD through a PCF8574 backpack; source assumes address
  `0x27` and P0=RS, P1=R/W, P2=E, P3=backlight, P4–P7=D4–D7.
- Controls: START PA0, MEASURE PA1, STOP PA2, active-low with MCU pull-ups.
- LEDs: green PB8, yellow PB9, red PB10; source drives each output high to turn
  its LED on.
- I²C1: PB6 SCL / PB7 SDA, AF4, open-drain. External pull-ups are required.

This table describes firmware configuration only. Verify each physical pin,
board revision, supply domain, common ground, module address, backpack mapping,
and pull-up arrangement before powering the assembly. Do not copy a wiring
diagram from the firmware pin map; the cable test-path topology is unknown.

## Electrical safety

Operate only with de-energized cable samples and a low-voltage, current-limited
test source. Do not connect this prototype to mains or an energized cable.
Disconnect power before changing leads. Confirm the INA219 shunt and check
that display/sensor pull-ups do not expose STM32 pins to an incompatible
voltage.

## Supplied materials

- [Prototype photograph](photos/hardware.jpeg): supplied image; connections
  are not validated by visual inspection.
- `wiring/` and `schematics/` are intentionally empty until verified drawings
  are provided.
