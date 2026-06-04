#ifndef INC_I2C_LCD_H_
#define INC_I2C_LCD_H_


#include <stdint.h>
#include "stm32f1xx_hal.h" // HAL: I2C_HandleTypeDef, HAL_StatusTypeDef, HAL_I2C_Master_Transmit, HAL_I2C_IsDeviceReady
#include "cmsis_os2.h" // CMSIS-RTOS2: osMutexId_t, osMutexAcquire, osMutexRelease

// Cấu hình LCD chuyên nghiệp
typedef struct {
    I2C_HandleTypeDef *hi2c;
    uint8_t i2c_addr;
    osMutexId_t mutex; // mutex RTOS (nếu NULL sẽ không bảo vệ)
} lcd_i2c_config_t;


// Khởi tạo LCD với config
HAL_StatusTypeDef lcd_init(const lcd_i2c_config_t *cfg);

// Gửi lệnh tới LCD
HAL_StatusTypeDef lcd_send_cmd(uint8_t cmd);

// Gửi dữ liệu tới LCD
HAL_StatusTypeDef lcd_send_data(uint8_t data);

// Gửi chuỗi tới LCD
HAL_StatusTypeDef lcd_send_string(const char *str);

// Đặt vị trí con trỏ
HAL_StatusTypeDef lcd_put_cur(uint8_t row, uint8_t col);

// Xóa màn hình
HAL_StatusTypeDef lcd_clear(void);

// Bật/tắt backlight
void lcd_backlight_on(void);
void lcd_backlight_off(void);

// Kiểm tra thiết bị sẵn sàng
uint8_t lcd_i2c_is_ready(uint8_t trials, uint32_t timeout);


#endif /* INC_I2C_LCD_H_ */
