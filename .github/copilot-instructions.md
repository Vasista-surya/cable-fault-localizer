# Repository guidance

- Preserve the STM32F401 CMSIS register-level implementation; do not migrate to
  HAL unless the project owner explicitly requests it.
- Distinguish implemented source behavior from planned or hardware-validated
  behavior.
- Never infer cable distance from current alone or introduce a fixed distance.
- Keep INA219 calibration assumptions in the user configuration section and
  require verification of the physical shunt.
- Do not add wiring diagrams unless the actual connections are verified.
- Keep experiment data blank until supplied by the team.
- Test only de-energized, current-limited, low-voltage cable samples.
