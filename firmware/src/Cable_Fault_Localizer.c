/*
 * Portable Cable Fault Localization and Distance Estimation Instrument
 * Target: STM32F401CCU6 (Black Pill), CMSIS device header stm32f4xx.h
 *
 * Scope of this implementation:
 * - Reads INA219 bus voltage and shunt/current registers over I2C1.
 * - Drives 16x2 I2C LCD (PCF8574 backpack) and three status LEDs.
 * - Uses active-low buttons: PA0=START, PA1=MEASURE, PA2=STOP.
 * - Indicates low current/no current in the configured test path. This does
 *   not distinguish an open cable from a disconnected source, load, or sensor.
 * - Does not calculate cable resistance or physical fault distance. A
 *   current-only reading does NOT uniquely determine either quantity.
 *
 * SAFETY: Test only de-energized, current-limited, low-voltage cable samples.
 *
 * IMPORTANT HARDWARE CONFIGURATION:
 * 1. INA219 I2C address 0x40; LCD backpack address 0x27 (confirm with scanner).
 * 2. Example INA219 calibration assumes a 0.1 ohm shunt; verify the actual
 *    module and set the calibration configuration accordingly.
 * 3. Set INA219_CAL_VALUE to the value derived for the installed shunt and
 *    selected current LSB. Do not copy a calibration value from a different
 *    board without checking its shunt.
 * 4. Do not connect an energized/high-voltage cable. Test only a de-energized,
 *    low-voltage, current-limited cable sample.
 *
 * LCD PCF8574 bit mapping assumed:
 *   P0=RS, P1=R/W, P2=E, P3=Backlight, P4..P7=D4..D7.
 * Confirm the backpack mapping before powering.
 */

#include "stm32f4xx.h"
#include <stdint.h>
#include <stdbool.h>

/* --------------------------- User configuration -------------------------- */
/* clock_init() selects HSI 16 MHz and APB1 /1 before any timed peripheral use. */
#define SYSCLK_HZ                   16000000UL
#define PCLK1_HZ                    16000000UL
#define I2C_STANDARD_MODE_HZ        100000UL
#define I2C_TIMEOUT                 100000UL
#define CLOCK_TIMEOUT               1000000UL
#define BUTTON_DEBOUNCE_MS          25U

#define LCD_I2C_ADDR                0x27U
#define INA219_I2C_ADDR             0x40U

#define LCD_RS                      (1U << 0)
#define LCD_RW                      (1U << 1)
#define LCD_EN                      (1U << 2)
#define LCD_BACKLIGHT               (1U << 3)

/* PCF8574 bit layout is backpack-dependent; verify before use. */
#define BUTTON_START_PIN            0U   /* PA0, active-low */
#define BUTTON_MEASURE_PIN          1U   /* PA1, active-low */
#define BUTTON_STOP_PIN             2U   /* PA2, active-low */

#define LED_GREEN_PIN               8U   /* PB8 */
#define LED_YELLOW_PIN              9U   /* PB9 */
#define LED_RED_PIN                 10U  /* PB10 */

/*
 * INA219 register map.
 */
#define INA219_REG_CONFIG            0x00U
#define INA219_REG_SHUNT_VOLTAGE     0x01U
#define INA219_REG_BUS_VOLTAGE       0x02U
#define INA219_REG_POWER             0x03U
#define INA219_REG_CURRENT           0x04U
#define INA219_REG_CALIBRATION       0x05U

/*
 * User-configurable INA219 assumptions. The supplied board's shunt value has
 * not been verified. Recalculate the calibration register for its actual
 * shunt before relying on current readings.
 *
 * CAL = trunc(0.04096 / (Current_LSB * R_SHUNT))
 * Current_LSB in A/bit; R_SHUNT in ohms.
 *
 * If Current_LSB = 100 uA/bit and R_SHUNT = 0.1 ohm:
 * CAL = 4096.
 */
#define INA219_CONFIG_VALUE          0x399FU
#define INA219_CAL_VALUE             4096U
#define INA219_CURRENT_LSB_UA        100U
#define INA219_SHUNT_UOHM            100000U
/* Provisional, user-configurable thresholds; they are not validated limits. */
#define INA219_LOW_CURRENT_MA        2U
#define INA219_RECOVERY_CURRENT_MA   3U
#define FAULT_CONFIRM_SAMPLES        5U
#define SAMPLE_PERIOD_MS             100U

/* Distance display stays uncalibrated until a validated circuit is implemented. */

/* ------------------------------- Types ------------------------------------ */
typedef enum
{
    APP_STATE_OFF = 0,
    APP_STATE_READY,
    APP_STATE_MEASURING,
    APP_STATE_RESULT,
    APP_STATE_ERROR
} AppState;

typedef enum
{
    FAULT_NONE = 0,
    FAULT_LOW_CURRENT_OR_NO_PATH,
    FAULT_SENSOR_COMMUNICATION,
    FAULT_INVALID_MEASUREMENT
} FaultStatus;

typedef struct
{
    int16_t raw_shunt;
    uint16_t raw_bus;
    int16_t raw_current;
    int32_t shunt_uV;
    uint32_t bus_mV;
    int32_t current_uA;
} Ina219Sample;

/* --------------------------- Global state --------------------------------- */
static AppState g_state = APP_STATE_OFF;
static FaultStatus g_fault = FAULT_NONE;
static Ina219Sample g_sample;
static bool g_low_current_active = false;
static uint64_t g_current_sum_uA = 0U;
static uint8_t g_current_sample_count = 0U;
static uint32_t g_filtered_current_uA = 0U;
static bool g_lcd_ok = true;
static uint8_t g_button_armed_mask = 0x07U;

/* --------------------------- Utility / delays ----------------------------- */
static bool clock_init(void)
{
    uint32_t timeout = CLOCK_TIMEOUT;

    RCC->CR |= 1U;
    while ((RCC->CR & (1U << 1)) == 0U)
    {
        if (timeout-- == 0U)
            return false;
    }

    RCC->CFGR &= ~3U;
    timeout = CLOCK_TIMEOUT;
    while ((RCC->CFGR & (3U << 2)) != 0U)
    {
        if (timeout-- == 0U)
            return false;
    }

    RCC->CR &= ~(1U << 24);
    timeout = CLOCK_TIMEOUT;
    while ((RCC->CR & (1U << 25)) != 0U)
    {
        if (timeout-- == 0U)
            return false;
    }

    RCC->CFGR &= ~((0xFU << 4) | (7U << 10) | (7U << 13));
    FLASH->ACR &= ~7U;
    return true;
}

static void delay_ms(uint32_t ms)
{
    while (ms-- > 0U)
    {
        SysTick->LOAD = (SYSCLK_HZ / 1000U) - 1U;
        SysTick->VAL = 0U;
        SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk |
                        SysTick_CTRL_ENABLE_Msk;
        while ((SysTick->CTRL & SysTick_CTRL_COUNTFLAG_Msk) == 0U)
        {
            /* wait for 1 ms */
        }
    }
    SysTick->CTRL = 0U;
}

static bool wait_flag_set(volatile uint32_t *reg, uint32_t mask)
{
    uint32_t timeout = I2C_TIMEOUT;
    while (((*reg) & mask) == 0U)
    {
        if ((I2C1->SR1 & (I2C_SR1_BERR | I2C_SR1_ARLO | I2C_SR1_AF |
                          I2C_SR1_OVR)) != 0U || timeout-- == 0U)
            return false;
    }
    return true;
}

static bool wait_flag_clear(volatile uint32_t *reg, uint32_t mask)
{
    uint32_t timeout = I2C_TIMEOUT;
    while (((*reg) & mask) != 0U)
    {
        if ((I2C1->SR1 & (I2C_SR1_BERR | I2C_SR1_ARLO | I2C_SR1_AF |
                          I2C_SR1_OVR)) != 0U || timeout-- == 0U)
            return false;
    }
    return true;
}

/* ------------------------------ GPIO -------------------------------------- */
static void gpio_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN;

    /* PA0..PA2: input with pull-ups (buttons connect to GND). */
    GPIOA->MODER &= ~((3U << 0) | (3U << 2) | (3U << 4));
    GPIOA->PUPDR &= ~((3U << 0) | (3U << 2) | (3U << 4));
    GPIOA->PUPDR |=  ((1U << 0) | (1U << 2) | (1U << 4));

    /* PB8..PB10: push-pull outputs. */
    GPIOB->MODER &= ~((3U << (LED_GREEN_PIN * 2U)) |
                      (3U << (LED_YELLOW_PIN * 2U)) |
                      (3U << (LED_RED_PIN * 2U)));
    GPIOB->MODER |=  ((1U << (LED_GREEN_PIN * 2U)) |
                      (1U << (LED_YELLOW_PIN * 2U)) |
                      (1U << (LED_RED_PIN * 2U)));

    GPIOB->OTYPER &= ~((1U << LED_GREEN_PIN) |
                       (1U << LED_YELLOW_PIN) |
                       (1U << LED_RED_PIN));

    GPIOB->OSPEEDR &= ~((3U << (LED_GREEN_PIN * 2U)) |
                        (3U << (LED_YELLOW_PIN * 2U)) |
                        (3U << (LED_RED_PIN * 2U)));

    GPIOB->BSRR = (1U << (LED_GREEN_PIN + 16U)) |
                  (1U << (LED_YELLOW_PIN + 16U)) |
                  (1U << (LED_RED_PIN + 16U));
}

static void led_on(uint8_t pin)
{
    GPIOB->BSRR = (1U << pin);
}

static void led_off(uint8_t pin)
{
    GPIOB->BSRR = (1U << (pin + 16U));
}

static void leds_off(void)
{
    led_off(LED_GREEN_PIN);
    led_off(LED_YELLOW_PIN);
    led_off(LED_RED_PIN);
}

static bool button_pressed(uint8_t pin)
{
    uint8_t mask = (uint8_t)(1U << pin);

    if ((GPIOA->IDR & mask) != 0U)
    {
        g_button_armed_mask |= mask;
        return false;
    }

    if ((g_button_armed_mask & mask) == 0U)
        return false;

    delay_ms(BUTTON_DEBOUNCE_MS);
    if ((GPIOA->IDR & mask) != 0U)
        return false;

    g_button_armed_mask &= (uint8_t)~mask;
    return true;
}

/* ------------------------------- I2C1 ------------------------------------- */
/* PB6=SCL, PB7=SDA, AF4, open-drain. External pull-ups are recommended. */
static void i2c1_recover(void)
{
    /* Peripheral reset cannot release a line held low by an external fault. */
    I2C1->CR1 = 0U;
    I2C1->CR1 = I2C_CR1_SWRST;
    delay_ms(1U);
    I2C1->CR1 = 0U;
    I2C1->CR2 = (PCLK1_HZ / 1000000U) & 0x3FU;
    I2C1->CCR = PCLK1_HZ / (2U * I2C_STANDARD_MODE_HZ);
    I2C1->TRISE = (PCLK1_HZ / 1000000U) + 1U;
    I2C1->CR1 = I2C_CR1_PE | I2C_CR1_ACK;
}

static void i2c1_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN;
    RCC->APB1ENR |= RCC_APB1ENR_I2C1EN;

    GPIOB->MODER &= ~((3U << 12) | (3U << 14));
    GPIOB->MODER |=  ((2U << 12) | (2U << 14));
    GPIOB->OTYPER |=  (1U << 6) | (1U << 7);
    GPIOB->OSPEEDR &= ~((3U << 12) | (3U << 14));
    GPIOB->PUPDR &= ~((3U << 12) | (3U << 14));
    GPIOB->AFR[0] &= ~((0xFU << 24) | (0xFU << 28));
    GPIOB->AFR[0] |=  ((4U << 24) | (4U << 28));

    /* Clock setup selects HSI 16 MHz with APB1 prescaler /1. */
    i2c1_recover();
}

static bool i2c_start(uint8_t address, bool read)
{
    if (!wait_flag_clear(&I2C1->SR2, I2C_SR2_BUSY))
    {
        i2c1_recover();
        return false;
    }

    I2C1->CR1 |= I2C_CR1_START;
    if (!wait_flag_set(&I2C1->SR1, I2C_SR1_SB))
    {
        i2c1_recover();
        return false;
    }

    I2C1->DR = (uint8_t)((address << 1) | (read ? 1U : 0U));
    if (!wait_flag_set(&I2C1->SR1, I2C_SR1_ADDR))
    {
        I2C1->CR1 |= I2C_CR1_STOP;
        i2c1_recover();
        return false;
    }

    (void)I2C1->SR2; /* clear ADDR */
    return true;
}

static void i2c_stop(void)
{
    I2C1->CR1 |= I2C_CR1_STOP;
}

static bool i2c_write_byte(uint8_t address, uint8_t data)
{
    if (!i2c_start(address, false))
        return false;

    if (!wait_flag_set(&I2C1->SR1, I2C_SR1_TXE))
    {
        i2c_stop();
        i2c1_recover();
        return false;
    }
    I2C1->DR = data;

    if (!wait_flag_set(&I2C1->SR1, I2C_SR1_BTF))
    {
        i2c_stop();
        i2c1_recover();
        return false;
    }
    i2c_stop();
    return true;
}

static bool i2c_write16(uint8_t address, uint8_t reg, uint16_t value)
{
    if (!i2c_start(address, false))
        return false;

    if (!wait_flag_set(&I2C1->SR1, I2C_SR1_TXE))
        goto fail;
    I2C1->DR = reg;

    if (!wait_flag_set(&I2C1->SR1, I2C_SR1_TXE))
        goto fail;
    I2C1->DR = (uint8_t)(value >> 8);

    if (!wait_flag_set(&I2C1->SR1, I2C_SR1_TXE))
        goto fail;
    I2C1->DR = (uint8_t)value;

    if (!wait_flag_set(&I2C1->SR1, I2C_SR1_BTF))
        goto fail;
    i2c_stop();
    return true;

fail:
    i2c_stop();
    i2c1_recover();
    return false;
}

/*
 * Read two bytes using a repeated START.
 * POS and ACK are sequenced for a two-byte STM32F4 master receive.
 */
static bool i2c_read16(uint8_t address, uint8_t reg, uint16_t *value)
{
    uint8_t msb, lsb;

    if (value == 0)
        return false;

    if (!i2c_start(address, false))
        return false;

    if (!wait_flag_set(&I2C1->SR1, I2C_SR1_TXE))
        goto fail;
    I2C1->DR = reg;

    if (!wait_flag_set(&I2C1->SR1, I2C_SR1_BTF))
        goto fail;

    I2C1->CR1 |= I2C_CR1_POS;
    I2C1->CR1 |= I2C_CR1_START;
    if (!wait_flag_set(&I2C1->SR1, I2C_SR1_SB))
        goto fail;

    I2C1->DR = (uint8_t)((address << 1) | 1U);
    if (!wait_flag_set(&I2C1->SR1, I2C_SR1_ADDR))
        goto fail;

    I2C1->CR1 &= ~I2C_CR1_ACK;
    (void)I2C1->SR2;

    if (!wait_flag_set(&I2C1->SR1, I2C_SR1_BTF))
        goto fail;

    i2c_stop();
    msb = (uint8_t)I2C1->DR;
    lsb = (uint8_t)I2C1->DR;

    I2C1->CR1 |= I2C_CR1_ACK;
    I2C1->CR1 &= ~I2C_CR1_POS;
    *value = (uint16_t)(((uint16_t)msb << 8) | lsb);
    return true;

fail:
    i2c_stop();
    I2C1->CR1 |= I2C_CR1_ACK;
    I2C1->CR1 &= ~I2C_CR1_POS;
    i2c1_recover();
    return false;
}

/* ------------------------------ LCD --------------------------------------- */
static void lcd_expander_write(uint8_t data)
{
    if (g_lcd_ok && !i2c_write_byte(LCD_I2C_ADDR,
                                    (uint8_t)(data | LCD_BACKLIGHT)))
        g_lcd_ok = false;
}

static void lcd_pulse(uint8_t data)
{
    lcd_expander_write((uint8_t)(data | LCD_EN));
    delay_ms(1U);
    lcd_expander_write((uint8_t)(data & (uint8_t)~LCD_EN));
    delay_ms(1U);
}

static void lcd_send_nibble(uint8_t nibble, bool rs)
{
    uint8_t out = (uint8_t)(nibble & 0xF0U);
    if (rs) out |= LCD_RS;
    lcd_expander_write(out);
    lcd_pulse(out);
}

static void lcd_command(uint8_t cmd)
{
    lcd_send_nibble((uint8_t)(cmd & 0xF0U), false);
    lcd_send_nibble((uint8_t)((cmd << 4) & 0xF0U), false);
    if (cmd == 0x01U || cmd == 0x02U)
        delay_ms(2U);
}

static void lcd_data(uint8_t data)
{
    lcd_send_nibble((uint8_t)(data & 0xF0U), true);
    lcd_send_nibble((uint8_t)((data << 4) & 0xF0U), true);
}

static void lcd_init(void)
{
    delay_ms(50U);
    lcd_send_nibble(0x30U, false);
    delay_ms(5U);
    lcd_send_nibble(0x30U, false);
    delay_ms(1U);
    lcd_send_nibble(0x30U, false);
    delay_ms(1U);
    lcd_send_nibble(0x20U, false);
    delay_ms(1U);

    lcd_command(0x28U); /* 4-bit, 2-line, 5x8 */
    lcd_command(0x0CU); /* display on, cursor off */
    lcd_command(0x06U); /* increment */
    lcd_command(0x01U); /* clear */
}

static void lcd_set_cursor(uint8_t row, uint8_t col)
{
    lcd_command((uint8_t)((row == 0U ? 0x80U : 0xC0U) + col));
}

static void lcd_print(const char *s)
{
    while (*s != '\0')
        lcd_data((uint8_t)*s++);
}

static void lcd_print_u32(uint32_t value)
{
    char buf[11];
    uint8_t i = 0U;
    if (value == 0U)
    {
        lcd_data('0');
        return;
    }
    while (value != 0U && i < sizeof(buf))
    {
        buf[i++] = (char)('0' + (value % 10U));
        value /= 10U;
    }
    while (i > 0U)
        lcd_data((uint8_t)buf[--i]);
}

static void lcd_clear(void)
{
    lcd_command(0x01U);
}

/* ----------------------------- INA219 ------------------------------------- */
static int16_t ina219_signed_word(uint16_t raw)
{
    if (raw <= INT16_MAX)
        return (int16_t)raw;
    return (int16_t)(-1 - (int32_t)(UINT16_MAX - raw));
}

static bool ina219_init(void)
{
    if (!i2c_write16(INA219_I2C_ADDR, INA219_REG_CONFIG,
                     INA219_CONFIG_VALUE))
        return false;

    if (!i2c_write16(INA219_I2C_ADDR, INA219_REG_CALIBRATION,
                     INA219_CAL_VALUE))
        return false;

    delay_ms(5U);
    return true;
}

static bool ina219_read_sample(Ina219Sample *sample)
{
    uint16_t raw;
    uint16_t raw_shunt;

    if (sample == 0)
        return false;

    if (!i2c_read16(INA219_I2C_ADDR, INA219_REG_SHUNT_VOLTAGE,
                    &raw_shunt))
        return false;

    if (!i2c_read16(INA219_I2C_ADDR, INA219_REG_BUS_VOLTAGE,
                    &sample->raw_bus))
        return false;

    if (!i2c_read16(INA219_I2C_ADDR, INA219_REG_CURRENT, &raw))
        return false;

    sample->raw_shunt = ina219_signed_word(raw_shunt);
    sample->raw_current = ina219_signed_word(raw);
    sample->shunt_uV = (int32_t)sample->raw_shunt * 10; /* 10 uV / bit */
    sample->bus_mV = (uint32_t)(sample->raw_bus >> 3) * 4U; /* 4 mV / bit */

    /*
     * Current register scaling: configured INA219 Current_LSB.
     * INA219 current register is signed. Use the real calibrated LSB.
     */
    sample->current_uA = (int32_t)sample->raw_current *
                         (int32_t)INA219_CURRENT_LSB_UA;

    return true;
}

/* ------------------------ Decision / calculation -------------------------- */
static bool evaluate_sample(void)
{
    if (g_sample.bus_mV == 0U ||
        (g_sample.raw_bus & (1U << 1)) == 0U ||
        (g_sample.raw_bus & 1U) != 0U)
    {
        g_fault = FAULT_INVALID_MEASUREMENT;
        return false;
    }

    g_current_sum_uA += (uint32_t)(g_sample.current_uA < 0 ?
                         -g_sample.current_uA : g_sample.current_uA);
    g_current_sample_count++;
    return true;
}

static void finalize_measurement(void)
{
    uint32_t low_threshold_uA = INA219_LOW_CURRENT_MA * 1000U;
    uint32_t recovery_threshold_uA = INA219_RECOVERY_CURRENT_MA * 1000U;

    g_filtered_current_uA = (uint32_t)(g_current_sum_uA /
                                       FAULT_CONFIRM_SAMPLES);
    g_current_sum_uA = 0U;
    g_current_sample_count = 0U;

    if (g_filtered_current_uA <= low_threshold_uA)
        g_low_current_active = true;
    else if (g_filtered_current_uA >= recovery_threshold_uA)
        g_low_current_active = false;

    g_fault = g_low_current_active ? FAULT_LOW_CURRENT_OR_NO_PATH : FAULT_NONE;
}

/* --------------------------- Display / UI --------------------------------- */
static void display_off(void)
{
    lcd_clear();
    lcd_set_cursor(0U, 0U);
    lcd_print("CABLE DIAGNOSTIC");
    lcd_set_cursor(1U, 0U);
    lcd_print("SW1: START");
}

static void display_ready(void)
{
    lcd_clear();
    lcd_set_cursor(0U, 0U);
    lcd_print("SYSTEM READY");
    lcd_set_cursor(1U, 0U);
    lcd_print("SW2: MEASURE");
}

static void display_measuring(void)
{
    lcd_clear();
    lcd_set_cursor(0U, 0U);
    lcd_print("MEASURING...");
    lcd_set_cursor(1U, 0U);
    lcd_print("Please wait");
}

static void display_result(void)
{
    lcd_clear();

    if (g_fault == FAULT_SENSOR_COMMUNICATION)
    {
        lcd_set_cursor(0U, 0U);
        lcd_print("INA219 I2C ERROR");
        lcd_set_cursor(1U, 0U);
        lcd_print("CHECK BUS/WIRING");
    }
    else if (g_fault == FAULT_INVALID_MEASUREMENT)
    {
        lcd_set_cursor(0U, 0U);
        lcd_print("INVALID BUS DATA");
        lcd_set_cursor(1U, 0U);
        lcd_print("CHECK SUPPLY");
    }
    else if (g_fault == FAULT_LOW_CURRENT_OR_NO_PATH)
    {
        lcd_set_cursor(0U, 0U);
        lcd_print("LOW CURRENT");
        lcd_set_cursor(1U, 0U);
        lcd_print("DIST: NOT CAL");
    }
    else
    {
        lcd_set_cursor(0U, 0U);
        lcd_print("CURRENT:");
        lcd_print_u32(g_filtered_current_uA / 1000U);
        lcd_print("mA");
        lcd_set_cursor(1U, 0U);
        lcd_print("BUS:");
        lcd_print_u32(g_sample.bus_mV);
        lcd_print("mV");
    }
}

static void update_indicators(void)
{
    leds_off();

    if (g_state == APP_STATE_MEASURING)
        led_on(LED_YELLOW_PIN);
    else if (g_fault == FAULT_LOW_CURRENT_OR_NO_PATH ||
             g_fault == FAULT_SENSOR_COMMUNICATION ||
             g_fault == FAULT_INVALID_MEASUREMENT ||
             g_state == APP_STATE_ERROR)
        led_on(LED_RED_PIN);
    else if (g_state == APP_STATE_READY || g_state == APP_STATE_RESULT)
        led_on(LED_GREEN_PIN);
}

/* -------------------------------- Main ------------------------------------ */
int main(void)
{
    gpio_init();
    if (!clock_init())
    {
        led_on(LED_RED_PIN);
        while (1)
        {
            /* Clock configuration failed; remain in the safe error state. */
        }
    }

    delay_ms(20U);
    i2c1_init();
    delay_ms(20U);

    lcd_init();

    lcd_set_cursor(0U, 0U);
    lcd_print("CABLE FAULT");
    lcd_set_cursor(1U, 0U);
    lcd_print("LOCALIZER");
    delay_ms(1200U);

    bool ina_ok = ina219_init();

    if (!ina_ok)
    {
        g_state = APP_STATE_ERROR;
        g_fault = FAULT_SENSOR_COMMUNICATION;
        display_result();
    }
    else
    {
        display_off();
        leds_off();
    }
    update_indicators();

    while (1)
    {
        if (!g_lcd_ok)
        {
            g_state = APP_STATE_ERROR;
            g_fault = FAULT_SENSOR_COMMUNICATION;
            update_indicators();
        }

        if (g_state == APP_STATE_OFF &&
            button_pressed(BUTTON_START_PIN))
        {
            g_low_current_active = false;
            g_filtered_current_uA = 0U;
            if (ina219_init())
            {
                g_fault = FAULT_NONE;
                g_state = APP_STATE_READY;
                display_ready();
            }
            else
            {
                g_fault = FAULT_SENSOR_COMMUNICATION;
                g_state = APP_STATE_ERROR;
                display_result();
            }
            update_indicators();
        }

        if ((g_state == APP_STATE_READY || g_state == APP_STATE_RESULT) &&
            button_pressed(BUTTON_MEASURE_PIN))
        {
            g_state = APP_STATE_MEASURING;
            g_fault = FAULT_NONE;
            g_current_sum_uA = 0U;
            g_current_sample_count = 0U;
            display_measuring();
            update_indicators();

            bool cancelled = false;
            for (uint8_t i = 0U; i < FAULT_CONFIRM_SAMPLES; i++)
            {
                if (button_pressed(BUTTON_STOP_PIN))
                {
                    cancelled = true;
                    break;
                }

                if (!ina219_read_sample(&g_sample))
                {
                    g_fault = FAULT_SENSOR_COMMUNICATION;
                    break;
                }

                if (!evaluate_sample())
                    break;

                if (i + 1U < FAULT_CONFIRM_SAMPLES)
                    delay_ms(SAMPLE_PERIOD_MS);
            }

            if (cancelled)
            {
                g_state = APP_STATE_OFF;
                g_fault = FAULT_NONE;
                g_low_current_active = false;
                g_current_sum_uA = 0U;
                g_current_sample_count = 0U;
                leds_off();
                display_off();
            }
            else
            {
                if (g_fault == FAULT_NONE &&
                    g_current_sample_count == FAULT_CONFIRM_SAMPLES)
                    finalize_measurement();

                g_state = (g_fault == FAULT_SENSOR_COMMUNICATION) ?
                          APP_STATE_ERROR : APP_STATE_RESULT;
                display_result();
                update_indicators();
            }

        }

        if (g_state != APP_STATE_OFF &&
            button_pressed(BUTTON_STOP_PIN))
        {
            g_state = APP_STATE_OFF;
            g_fault = FAULT_NONE;
            g_low_current_active = false;
            g_current_sum_uA = 0U;
            g_current_sample_count = 0U;
            leds_off();
            display_off();
        }

        delay_ms(10U);
    }
}
