Thư viện điều khiển servo SG90 cho STM32 (PWM TIM).

## Cách nối dây (board hiện tại)

| Servo | TIM | Kênh | Chân |
|-------|-----|------|------|
| 1     | TIM1 | CH3 | PA10 |
| 2     | TIM1 | CH4 | PA11 |

> CubeMX cấu hình TIM1 50 Hz (chu kỳ 20 ms). Xung ~1000–2000 tick ≈ 1–2 ms.

## Cách dùng nhanh

1. Copy thư mục `servo_lib` vào project.
2. Thêm include path và source path tới `servo_lib` (đã có trong `.cproject`).
3. `#include "servo.h"`

## Cấu hình số lượng servo

Trong `servo.h` (hoặc define trước khi include):

```c
#define SERVO_COUNT  2U   /* đổi thành 3, 4... khi thêm servo */
```

`SERVO_MAX_COUNT` (mặc định 4) là giới hạn mảng — tăng nếu cần > 4 servo.

## Struct cấu hình (giống `lcd_i2c_config_t`)

```c
servo_config_t servo_cfg = SERVO_BOARD_DEFAULT_CONFIG(&htim1);

/* Hoặc tự khai báo khi thêm servo 3: */
servo_config_t servo_cfg = {
    .count = 3U,
    .ch = {
        { .htim = &htim1, .channel = TIM_CHANNEL_3 },
        { .htim = &htim1, .channel = TIM_CHANNEL_4 },
        { .htim = &htim2, .channel = TIM_CHANNEL_1 },
    },
    .pulse_min = 0U,   /* 0 = dùng SERVO_PULSE_MIN_TICKS */
    .pulse_max = 0U,
};
```

## Init

```c
//khai báo 
servo_t servo;

servo_defaults(&servo);
servo_apply_cal(&servo, &my_cal);   /* tùy chọn, trước hoặc sau init */
if (!servo_init(&servo, &servo_cfg))
{
    Error_Handler();
}
servo_all_off(&servo);
servo_sync_present(&servo);
```

## Cal (góc đóng/mở đã học)

```c
servo_cal_t cal;

cal.close_deg[0] = 25U;
cal.open_deg[0]  = 90U;
cal.close_deg[1] = 30U;
cal.open_deg[1]  = 120U;
servo_apply_cal(&servo, &cal);
```

## API chính (theo chỉ số 0..count-1)

| Hàm | Mô tả |
|-----|--------|
| `servo_init()` | Bind TIM, start PWM |
| `servo_apply_cal()` | Nạp góc đóng/mở |
| `servo_set_angle(idx, deg)` | Đặt góc đích (có ramp) |
| `servo_set_angle_live()` | Áp ngay, bỏ ramp |
| `servo_set_learn_preview()` | Menu học góc 0..180 |
| `servo_ramp_update()` | Gọi ~20 ms |
| `servo_resync_all()` | Sau khi thoát học |
| `servo_close_deg()` / `servo_open_deg()` | Góc cal |
| `servo_angle_from_percent()` | 0%=đóng, 100%=mở |

## Ví dụ trong task output

```c
servo_apply_cal(&g_output.servo, &g_menu_ctx.servo_cal);

if (learn_active)
{
    servo_set_learn_preview(&g_output.servo, learn_idx, angle_deg);
}
else
{
    servo_ramp_update(&g_output.servo, osKernelGetTickCount());
    servo_set_angle(&g_output.servo, 0U, target_deg);
}
```

## Mở rộng thêm servo

1. Cấu hình thêm kênh TIM trong CubeMX.
2. Tăng `SERVO_COUNT`.
3. Thêm phần tử `.ch[]` trong `servo_config_t`.
4. Cập nhật menu học / Flash nếu cần lưu cal servo mới.
