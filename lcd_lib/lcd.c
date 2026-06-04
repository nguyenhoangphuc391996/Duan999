/** Put this in the src folder **/

#include <lcd.h>
#include "cmsis_os.h"
#include "itm.h"
#include <stdio.h>



// Macro chọn delay phù hợp cho RTOS hoặc baremetal
#ifdef USE_HAL_DELAY_FOR_INIT
#define LCD_DELAY(x) HAL_Delay(x)
#else
#define LCD_DELAY(x) osDelay(x)
#endif

/* PCF8574 -> HD44780 mapping (common for I2C backpacks):
 * P0: RS
 * P1: RW (typically tied low)
 * P2: EN
 * P3: Backlight
 * P4..P7: LCD data D4..D7
 */
#define LCD_PCF_RS        (0x01u)
#define LCD_PCF_EN        (0x04u)
#define LCD_PCF_BL        (0x08u)

static lcd_i2c_config_t lcd_cfg = {0};
static uint8_t lcd_backlight = 0x08; // 0x08: backlight on, 0x00: off

// Helper lock/unlock mutex nếu có
static void lcd_mutex_lock(void) {
    if (lcd_cfg.mutex) osMutexAcquire(lcd_cfg.mutex, osWaitForever);
}
static void lcd_mutex_unlock(void) {
    if (lcd_cfg.mutex) osMutexRelease(lcd_cfg.mutex);
}

// Helper truyền I2C với retry và mutex
static HAL_StatusTypeDef lcd_i2c_tx(uint8_t *data, uint16_t size, uint32_t timeout) {
    HAL_StatusTypeDef st = HAL_ERROR;
    lcd_mutex_lock();
    for (int i = 0; i < 3; ++i) {
        st = HAL_I2C_Master_Transmit(lcd_cfg.hi2c, lcd_cfg.i2c_addr, data, size, timeout);
        if (st == HAL_OK) break;
        LCD_DELAY(2);
    }
    lcd_mutex_unlock();
    return st;
}

static HAL_StatusTypeDef lcd_apply_backlight(void)
{
    uint8_t data = lcd_backlight;
    return lcd_i2c_tx(&data, 1, 100);
}

uint8_t lcd_i2c_is_ready(uint8_t trials, uint32_t timeout) {
    if (!lcd_cfg.hi2c) return 0;
    for (uint8_t i = 0; i < trials; ++i) {
        if (HAL_I2C_IsDeviceReady(lcd_cfg.hi2c, lcd_cfg.i2c_addr, 1, timeout) == HAL_OK) return 1;
        LCD_DELAY(5);
    }
    return 0;
}

void lcd_backlight_on(void)
{
    lcd_backlight = LCD_PCF_BL;
    (void)lcd_apply_backlight();
}

void lcd_backlight_off(void)
{
    lcd_backlight = 0x00;
    (void)lcd_apply_backlight();
}

static HAL_StatusTypeDef lcd_send_nibble(uint8_t nibble, uint8_t rs)
{
    /* nibble is expected in bits [4..7] (e.g. 0x30 -> high nibble). */
    uint8_t data = (nibble & 0xF0u) | lcd_backlight;
    if (rs != 0u)
    {
        data |= LCD_PCF_RS;
    }

    uint8_t data_t[2];
    data_t[0] = data | LCD_PCF_EN; /* EN=1 */
    data_t[1] = data & (uint8_t)~LCD_PCF_EN; /* EN=0 */
    return lcd_i2c_tx(data_t, 2, 100);
}

HAL_StatusTypeDef lcd_send_cmd(uint8_t cmd) {
    uint8_t data_u = (cmd & 0xF0);
    uint8_t data_l = ((cmd << 4) & 0xF0);
    uint8_t data_t[4];
    data_t[0] = data_u | lcd_backlight | LCD_PCF_EN; // EN=1, RS=0
    data_t[1] = data_u | lcd_backlight;                // EN=0, RS=0
    data_t[2] = data_l | lcd_backlight | LCD_PCF_EN;
    data_t[3] = data_l | lcd_backlight;
    HAL_StatusTypeDef st = lcd_i2c_tx(data_t, 4, 100);
    return st;
}

HAL_StatusTypeDef lcd_send_data(uint8_t data) {
    uint8_t data_u = (data & 0xF0);
    uint8_t data_l = ((data << 4) & 0xF0);
    uint8_t data_t[4];
    data_t[0] = data_u | lcd_backlight | LCD_PCF_EN | LCD_PCF_RS; // EN=1, RS=1
    data_t[1] = data_u | lcd_backlight | LCD_PCF_RS;              // EN=0, RS=1
    data_t[2] = data_l | lcd_backlight | LCD_PCF_EN | LCD_PCF_RS;
    data_t[3] = data_l | lcd_backlight | LCD_PCF_RS;
    HAL_StatusTypeDef st = lcd_i2c_tx(data_t, 4, 100);
    return st;
}

HAL_StatusTypeDef lcd_clear(void) {
    HAL_StatusTypeDef st = lcd_send_cmd(0x01);
    /* Clear command needs > 1.52ms; use a safer delay. */
    LCD_DELAY(5);
    return st;
}

HAL_StatusTypeDef lcd_put_cur(uint8_t row, uint8_t col) {
    uint8_t pos = (row == 0) ? (0x80 + (col & 0x0F)) : (0xC0 + (col & 0x0F));
    return lcd_send_cmd(pos);
}

HAL_StatusTypeDef lcd_send_string(const char *str) {
    HAL_StatusTypeDef st = HAL_OK;
    while (*str) {
        if (lcd_send_data((uint8_t)(*str++)) != HAL_OK) st = HAL_ERROR;
    }
    return st;
}

HAL_StatusTypeDef lcd_init(const lcd_i2c_config_t *cfg) {
    if (!cfg || !cfg->hi2c) return HAL_ERROR;
    lcd_cfg = *cfg;
    lcd_backlight = 0x08;

    /* Wait for LCD power-up.
       500ms is usually plenty; still keep it. */
    LCD_DELAY(500);

    /* Ensure backlight is on during init */
    lcd_backlight = LCD_PCF_BL;

    /* HD44780 4-bit init via PCF8574:
     * Use "nibble-only" sequence: 0x30 (3 times), then 0x20.
     */
    if (lcd_send_nibble(0x30u, 0u) != HAL_OK) return HAL_ERROR;
    LCD_DELAY(5);
    if (lcd_send_nibble(0x30u, 0u) != HAL_OK) return HAL_ERROR;
    LCD_DELAY(1);
    if (lcd_send_nibble(0x30u, 0u) != HAL_OK) return HAL_ERROR;
    LCD_DELAY(10);
    if (lcd_send_nibble(0x20u, 0u) != HAL_OK) return HAL_ERROR;
    LCD_DELAY(10);

    /* Now LCD should be in 4-bit mode; send full commands */
    if (lcd_send_cmd(0x28u) != HAL_OK) return HAL_ERROR; /* 4-bit, 2-line, 5x8 dots */
    LCD_DELAY(1);
    if (lcd_send_cmd(0x08u) != HAL_OK) return HAL_ERROR; /* display off */
    LCD_DELAY(1);
    if (lcd_send_cmd(0x01u) != HAL_OK) return HAL_ERROR; /* clear (we'll also delay in lcd_clear) */
    LCD_DELAY(5);
    if (lcd_send_cmd(0x06u) != HAL_OK) return HAL_ERROR; /* entry mode */
    LCD_DELAY(1);
    if (lcd_send_cmd(0x0Cu) != HAL_OK) return HAL_ERROR; /* display on, cursor off, blink off */
    LCD_DELAY(1);
    return HAL_OK;
}
