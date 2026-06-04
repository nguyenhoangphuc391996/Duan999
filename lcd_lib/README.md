Thư viện LCD I2C cho STM32, dùng được với CMSIS-RTOS2.

## Cách nối dây
  - `VCC` nối 5V
  - `GND` nối GND
  - `SDA` nối chân SDA của I2C (ví dụ PB7)
  - `SCL` nối chân SCL của I2C (ví dụ PB6)

> Mặc định địa chỉ I2C thường là `0x27` hoặc `0x3F`.

## Cách dùng nhanh

1. Copy thư mục `lcd_lib` vào project.
2. Thêm include path tới `lcd_lib`.
3. Thêm source path tơi `lcd_lib`.

4. Trong code:

```c
#include "lcd.h"
#include "itm.h"
```


## struct cấu hình lcd
```c
lcd_i2c_config_t lcd_cfg = {
    .hi2c = &hi2c1,
    .i2c_addr = 0x27 << 1,
    .mutex = MutexI2C1Handle,
};

## init lcd
lcd_init(&lcd_cfg);
```

## Ví dụ dùng trong task CMSIS v2

```c
void StartTaskLCD(void *argument)
{
    lcd_i2c_config_t lcd_cfg = {
        .hi2c = &hi2c1,
        .i2c_addr = 0x27 << 1,
        .mutex = MutexI2C1Handle,
    };

    if (lcd_init(&lcd_cfg) != HAL_OK) Error_Handler();

    lcd_clear();
    lcd_put_cur(0, 0);
    lcd_send_string("LCD init OK");
    lcd_put_cur(1, 0);
    lcd_send_string("LCD_V4 Test");
    osDelay(2000);
    itm_print("LCD init OK\r\n");

    for (;;)
    {
        osDelay(1);
    }
}
```

## API chính

- `lcd_init()`
- `lcd_clear()`
- `lcd_put_cur(row, col)`
- `lcd_send_string("...")`
- `lcd_send_cmd(cmd)`
- `lcd_send_data(ch)`
- `lcd_backlight_on()` / `lcd_backlight_off()`
- `lcd_i2c_is_ready(trials, timeout)`

## Lưu ý

- Nên gọi `lcd_init()` trong task sau khi kernel đã chạy.
- Nếu không cần mutex, có thể để `.mutex = NULL`.
- Nếu LCD không lên, kiểm tra lại dây `SDA/SCL`, nguồn và địa chỉ I2C.