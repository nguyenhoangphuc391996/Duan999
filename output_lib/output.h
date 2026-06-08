/*
 * output.h
 *
 *  Created on: 2 thg 6, 2026
 *      Author: embedded
 */

#ifndef OUTPUT_H_
#define OUTPUT_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "stm32f1xx_hal.h"

/**
 * @file output.h
 * @brief Điều khiển đầu ra: quạt PWM, servo SG90, đèn, điện trở, phun sương.
 *
 * CubeMX đã cấu hình GPIO/TIM; thư viện chỉ gán mức và khởi động PWM.
 * Đổi chân: sửa macro OUTPUT_DEFAULT_* hoặc gán lại trường trong @ref output_t
 * trước khi gọi @ref output_init.
 *
 * Mapping mặc định:
 * - PA15 / TIM2_CH1 : quạt 12V (PWM)
 * - PA10 / TIM1_CH3 : servo 1
 * - PA11 / TIM1_CH4 : servo 2
 * - PA12            : đèn
 * - PA8             : điện trở (sưởi)
 * - PB15            : phun sương
 */

/**
 * Góc servo đóng / mở (độ, thường 0..180).
 *
 * MIN_DEG = đóng, ánh xạ OUTPUT_SERVO_PULSE_MIN_TICKS (~1 ms).
 * MAX_DEG = mở,   ánh xạ OUTPUT_SERVO_PULSE_MAX_TICKS (~2 ms).
 *
 * MIN < MAX (vd. 60 → 120): đóng→mở quay tăng góc.
 * MIN > MAX (vd. 120 → 60): đóng→mở quay giảm góc / lắp ngược.
 *
 * Mọi điều khiển (Nghỉ, CO₂, warmup, Thanh trùng) dùng hai macro này.
 * Thanh trùng: cài % mở (0% = MIN, 100% = MAX) qua @ref output_servo_angle_from_percent.
 *
 * Chỉnh góc: sửa OUTPUT_SERVO_ANGLE_*_DEG_CFG bên dưới (số nguyên, không cast).
 */
/** Góc đóng / mở (độ) — giá trị cấu hình cho preprocessor. */
#define OUTPUT_SERVO_ANGLE_MIN_DEG_CFG  30
#define OUTPUT_SERVO_ANGLE_MAX_DEG_CFG  90

#if (OUTPUT_SERVO_ANGLE_MIN_DEG_CFG == OUTPUT_SERVO_ANGLE_MAX_DEG_CFG)
#error OUTPUT_SERVO_ANGLE_MIN_DEG_CFG and OUTPUT_SERVO_ANGLE_MAX_DEG_CFG must differ
#endif

#define OUTPUT_SERVO_ANGLE_MIN_DEG      ((uint8_t)OUTPUT_SERVO_ANGLE_MIN_DEG_CFG)
#define OUTPUT_SERVO_ANGLE_MAX_DEG      ((uint8_t)OUTPUT_SERVO_ANGLE_MAX_DEG_CFG)

/** Thời gian quay đều từ 0% (MIN) đến 100% (MAX), tránh sốc cơ khí. */
#define OUTPUT_SERVO_RAMP_FULL_MS       (5000U)

/**
 * Xung servo mặc định (đơn vị tick TIM), tương ứng TIM1 50 Hz, chu kỳ 20000.
 * ~1 ms / ~2 ms tương ứng góc 0° / 180° cho SG90.
 */
#define OUTPUT_SERVO_PULSE_MIN_TICKS    ((uint16_t)1000U)
#define OUTPUT_SERVO_PULSE_MAX_TICKS    ((uint16_t)2000U)

/** Chu kỳ PWM servo mặc định (ARR + 1), khớp MX_TIM1_Init. */
#define OUTPUT_SERVO_PERIOD_TICKS       ((uint16_t)20000U)

/** Chu kỳ PWM quạt mặc định (ARR + 1), khớp MX_TIM2_Init. */
#define OUTPUT_FAN_PERIOD_TICKS         ((uint16_t)160U)

/* --- Chân GPIO mặc định (đổi tại đây nếu đổi board) --- */
#define OUTPUT_DEFAULT_HEATER_PORT      GPIOA
#define OUTPUT_DEFAULT_HEATER_PIN       GPIO_PIN_8
#define OUTPUT_DEFAULT_LAMP_PORT        GPIOA
#define OUTPUT_DEFAULT_LAMP_PIN         GPIO_PIN_12
#define OUTPUT_DEFAULT_MIST_PORT        GPIOB
#define OUTPUT_DEFAULT_MIST_PIN         GPIO_PIN_15

#define OUTPUT_DEFAULT_FAN_TIM          TIM2
#define OUTPUT_DEFAULT_FAN_CHANNEL      TIM_CHANNEL_1

#define OUTPUT_DEFAULT_SERVO_TIM        TIM1
#define OUTPUT_DEFAULT_SERVO1_CHANNEL   TIM_CHANNEL_3
#define OUTPUT_DEFAULT_SERVO2_CHANNEL   TIM_CHANNEL_4

typedef struct
{
	GPIO_TypeDef *port;
	uint16_t pin;
} output_gpio_t;

typedef struct
{
	TIM_HandleTypeDef *htim;
	uint32_t channel;
} output_pwm_ch_t;

typedef enum
{
	OUTPUT_CMD_NONE = 0,
	OUTPUT_CMD_ALL_OFF,
	OUTPUT_CMD_HEATER_SET,
	OUTPUT_CMD_LAMP_SET,
	OUTPUT_CMD_MIST_SET,
	OUTPUT_CMD_FAN_SET_PERCENT,
	OUTPUT_CMD_SERVO1_SET_ANGLE,
	OUTPUT_CMD_SERVO2_SET_ANGLE,
	OUTPUT_CMD_SERVO1_SET_PULSE,
	OUTPUT_CMD_SERVO2_SET_PULSE
} output_cmd_id_t;

typedef struct
{
	output_cmd_id_t id;
	int16_t value;
} output_cmd_t;

/** Góc đóng/mở đã học cho từng servo (lưu Flash). */
typedef struct
{
	uint8_t servo1_close_deg;
	uint8_t servo1_open_deg;
	uint8_t servo2_close_deg;
	uint8_t servo2_open_deg;
} output_servo_cal_t;

/**
 * @brief Handle điều khiển toàn bộ đầu ra.
 *
 * Gọi @ref output_defaults rồi gán `htim_fan`, `htim_servo` (con trỏ từ main)
 * trước @ref output_init.
 */
typedef struct
{
	output_gpio_t pin_heater;
	output_gpio_t pin_lamp;
	output_gpio_t pin_mist;

	output_pwm_ch_t fan;
	output_pwm_ch_t servo1;
	output_pwm_ch_t servo2;

	/** true: ON = mức cao GPIO; false: ON = mức thấp. */
	bool gpio_active_high;

	/** 0 => dùng OUTPUT_SERVO_PULSE_*_TICKS. */
	uint16_t servo_pulse_min;
	uint16_t servo_pulse_max;
	/** 0 => đọc ARR từ TIM lúc init. */
	uint16_t servo_period_ticks;
	/** 0 => đọc ARR từ TIM lúc init. */
	uint16_t fan_period_ticks;

	/* Trạng thái (chỉ đọc sau init) */
	uint8_t fan_percent;
	uint8_t servo1_angle_deg;
	uint8_t servo2_angle_deg;
	int16_t servo1_permille;
	int16_t servo2_permille;
	int16_t servo1_target_permille;
	int16_t servo2_target_permille;
	uint32_t servo_ramp_last_tick;
	uint8_t servo1_close_deg;
	uint8_t servo1_open_deg;
	uint8_t servo2_close_deg;
	uint8_t servo2_open_deg;
	bool heater_on;
	bool lamp_on;
	bool mist_on;
	bool initialized;
} output_t;

/**
 * @brief Gán cấu hình mặc định (chân, kênh TIM). Chưa khởi động PWM.
 */
void output_defaults(output_t *h);

/**
 * @brief Khởi tạo: tắt GPIO, start PWM, đặt mức an toàn.
 * @return false nếu tham số NULL hoặc thiếu `htim_fan` / `htim_servo`.
 */
bool output_init(output_t *h);

/** Tắt toàn bộ đầu ra (GPIO + PWM 0 / servo về OUTPUT_SERVO_ANGLE_MIN_DEG). */
void output_all_off(output_t *h);

/* --- GPIO: điện trở, đèn, phun sương --- */
void output_heater_set(output_t *h, bool on);
void output_lamp_set(output_t *h, bool on);
void output_mist_set(output_t *h, bool on);

bool output_heater_get(const output_t *h);
bool output_lamp_get(const output_t *h);
bool output_mist_get(const output_t *h);

/* --- Quạt PWM: 0..100 % --- */
void output_fan_set_percent(output_t *h, uint8_t percent);
uint8_t output_fan_get_percent(const output_t *h);

/* --- Servo: góc (đóng/mở đã học) hoặc xung tick TIM --- */
void output_servo_apply_cal(output_t *h, const output_servo_cal_t *cal);
uint8_t output_servo_close_deg(const output_t *h, uint8_t servo_idx);
uint8_t output_servo_open_deg(const output_t *h, uint8_t servo_idx);
/** @brief servo_idx 0=servo1, 1=servo2; 0%=đóng, 100%=mở. */
uint8_t output_servo_angle_from_percent(const output_t *h, uint8_t servo_idx, uint8_t percent);
/** @brief servo_num 1 hoặc 2 — áp góc ngay (học góc, bỏ qua ramp). */
void output_servo_set_angle_live(output_t *h, uint8_t servo_num, uint8_t angle_deg);

void output_servo1_set_angle(output_t *h, uint8_t angle_deg);
void output_servo2_set_angle(output_t *h, uint8_t angle_deg);
void output_servo1_set_pulse_ticks(output_t *h, uint16_t pulse_ticks);
void output_servo2_set_pulse_ticks(output_t *h, uint16_t pulse_ticks);

uint8_t output_servo1_get_angle(const output_t *h);
uint8_t output_servo2_get_angle(const output_t *h);

/**
 * @brief Tiến ramp servo về góc đích (gọi định kỳ từ task output, ~20 ms).
 * @param now_tick @ref osKernelGetTickCount
 */
void output_servo_ramp_update(output_t *h, uint32_t now_tick);

/**
 * @brief Áp dụng 1 lệnh output (dùng cho queue/task output).
 * @return true nếu lệnh hợp lệ và đã xử lý, false nếu lệnh không hợp lệ.
 */
bool output_apply_cmd(output_t *h, const output_cmd_t *cmd);

#ifdef __cplusplus
}
#endif

#endif /* OUTPUT_H_ */
