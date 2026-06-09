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
#include "servo.h"

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
 *
 * Servo SG90: xem @ref servo.h / servo_lib.
 */

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

/**
 * @brief Handle điều khiển toàn bộ đầu ra.
 *
 * Gọi @ref output_defaults, @ref servo_init (xem servo_lib/README.md), rồi @ref output_init.
 */
typedef struct
{
	output_gpio_t pin_heater;
	output_gpio_t pin_lamp;
	output_gpio_t pin_mist;

	output_pwm_ch_t fan;
	servo_t servo;

	/** true: ON = mức cao GPIO; false: ON = mức thấp. */
	bool gpio_active_high;

	/** 0 => đọc ARR từ TIM lúc init. */
	uint16_t fan_period_ticks;

	/* Trạng thái (chỉ đọc sau init) */
	uint8_t fan_percent;
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
 * @return false nếu tham số NULL, thiếu `fan.htim`, hoặc servo chưa @ref servo_init.
 */
bool output_init(output_t *h);

/** Tắt toàn bộ đầu ra (GPIO + PWM 0 / servo về góc đóng). */
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

/**
 * @brief Áp dụng 1 lệnh output (dùng cho queue/task output).
 * @return true nếu lệnh hợp lệ và đã xử lý, false nếu lệnh không hợp lệ.
 */
bool output_apply_cmd(output_t *h, const output_cmd_t *cmd);

#ifdef __cplusplus
}
#endif

#endif /* OUTPUT_H_ */
