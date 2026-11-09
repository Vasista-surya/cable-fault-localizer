# Project report: Portable Cable Fault Localization and Distance Estimation Instrument

## Summary

This repository contains an STM32F401CCU6 CMSIS register-level firmware
prototype for acquiring INA219 current/shunt/bus-voltage readings and showing
status on an I²C 16×2 LCD, with three push buttons and three status LEDs.

The supplied firmware's supported observation is sustained low current in the
configured test path. It cannot establish why current is low, and does not
implement validated fault classification or physical distance localization.

## Hardware and software record

The target and firmware-configured pins, addresses, LCD expander mapping, and
clock configuration are documented in the root [README](../README.md) and
[hardware notes](../hardware/README.md). These are configuration assumptions,
not confirmed pin-to-pin wiring. The provided photograph is an appearance
record only. The repository contains no verified electrical schematic or
complete build project.

The firmware uses HSI at 16 MHz with APB1 /1, reads INA219 shunt/current/bus
registers over I²C1, and uses five-sample current averaging with provisional
entry/recovery thresholds. INA219 calibration depends on the physical module
shunt; the source's example values assume 0.1 Ω and 100 µA/bit and must be
checked.

## Measurement model and limitations

A current reading alone does not determine cable resistance or fault distance.
Distance estimation requires a defined test path, voltage and current
measurements across the same defined cable section, a cable-specific
resistance-per-length calibration, and compensation for shunt, leads, and
contacts. None of those complete validated conditions is included in the
supplied hardware materials. Firmware does not calculate distance.

No short-to-ground, short-between-conductors, resistive-fault, intermittent-
fault, accuracy, or supported cable-range claim is made.

## Safety and validation

Testing is limited to de-energized, current-limited, low-voltage cable samples.
There are no experimental results in the supplied materials. Record actual
measurements only after following the [calibration procedure](calibration_procedure.md)
and use the blank [validation log](validation_log_template.csv). No performance
claim should be added until supported by real test evidence.

## Evidence supplied

- Firmware source: [`Cable_Fault_Localizer.c`](../firmware/src/Cable_Fault_Localizer.c).
- Slide deck: [`Project_Presentation.pptx`](Project_Presentation.pptx).
- Prototype appearance photo: [`hardware.jpeg`](../hardware/photos/hardware.jpeg).

This report documents the available source and materials; it is not a report
of hardware validation.
