# Firmware

`src/Cable_Fault_Localizer.c` is the supplied CMSIS register-level C
application for STM32F401. It uses no HAL. The repository does not include
startup code, linker script, CMSIS device headers, a Keil project, or a GCC
build file; see the root [build notes](../README.md#firmware-and-build) before
attempting a complete image build.

## Clock and peripherals

`clock_init()` selects HSI as the system clock and uses AHB/APB prescalers of
/1. The source therefore configures SysTick and I²C1 for 16 MHz. PB6/PB7 are
configured as AF4 open-drain. External pull-ups appropriate for the actual
bus voltage and connected modules are required; the source does not establish
their value or verify their presence.

The LCD address, INA219 address, PCF8574 bit mapping, INA219 shunt/current-LSB
assumptions, and current thresholds are in the user configuration section at
the top of the source. Verify the backpack mapping, device addresses, and
physical INA219 shunt before relying on readings.

## Behavior and limitations

The firmware acquires five INA219 samples and uses their mean absolute current
with configurable low-current and recovery thresholds. The thresholds are
provisional. A sustained low reading means low current in the current test
setup, not proof of an open cable. The bus-voltage validity check cannot
confirm that the external source, load, or cable topology is correct.

The firmware does not implement short-to-ground, short-between-conductors,
resistive-fault, intermittent-fault, or physical distance localization. It
does not calculate distance; distance is not calibrated. STOP returns the
application to OFF and can cancel the sample sequence. I²C transactions are
bounded and a peripheral reset is attempted on error; a physically stuck-low
bus requires inspection and may require a power cycle.

**Safety:** connect only de-energized, current-limited, low-voltage cable
samples. No hardware validation evidence was supplied with this source.
