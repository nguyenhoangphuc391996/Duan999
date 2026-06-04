/*
 * output.c
 *
 *  Created on: 2 thg 6, 2026
 *      Author: embedded
 */

#include "output.h"

static uint8_t output_clamp_u8(uint8_t value, uint8_t min_v, uint8_t max_v)
{
	if (value < min_v)
	{
		return min_v;
	}
	if (value > max_v)
	{
		return max_v;
	}
	return value;
}

static uint16_t output_tim_period_ticks(const TIM_HandleTypeDef *htim, uint16_t configured)
{
	if (configured != 0U)
	{
		return configured;
	}
	if ((htim == NULL) || (htim->Instance == NULL))
	{
		return 0U;
	}
	return (uint16_t)(__HAL_TIM_GET_AUTORELOAD(htim) + 1U);
}

static uint16_t output_servo_pulse_min(const output_t *h)
{
	if (h->servo_pulse_min != 0U)
	{
		return h->servo_pulse_min;
	}
	return OUTPUT_SERVO_PULSE_MIN_TICKS;
}

static uint16_t output_servo_pulse_max(const output_t *h)
{
	if (h->servo_pulse_max != 0U)
	{
		return h->servo_pulse_max;
	}
	return OUTPUT_SERVO_PULSE_MAX_TICKS;
}

static void output_gpio_write(const output_gpio_t *gpio, bool active_high, bool on)
{
	GPIO_PinState level;

	if ((gpio == NULL) || (gpio->port == NULL))
	{
		return;
	}

	if (active_high)
	{
		level = on ? GPIO_PIN_SET : GPIO_PIN_RESET;
	}
	else
	{
		level = on ? GPIO_PIN_RESET : GPIO_PIN_SET;
	}
	HAL_GPIO_WritePin(gpio->port, gpio->pin, level);
}

static bool output_gpio_read(const output_gpio_t *gpio, bool active_high)
{
	GPIO_PinState level;

	if ((gpio == NULL) || (gpio->port == NULL))
	{
		return false;
	}

	level = HAL_GPIO_ReadPin(gpio->port, gpio->pin);
	if (active_high)
	{
		return (level == GPIO_PIN_SET);
	}
	return (level == GPIO_PIN_RESET);
}

static void output_pwm_set_compare(output_pwm_ch_t *ch, uint16_t pulse_ticks)
{
	if ((ch == NULL) || (ch->htim == NULL) || (ch->htim->Instance == NULL))
	{
		return;
	}
	__HAL_TIM_SET_COMPARE(ch->htim, ch->channel, pulse_ticks);
}

static bool output_pwm_start(output_pwm_ch_t *ch)
{
	if ((ch == NULL) || (ch->htim == NULL))
	{
		return false;
	}
	return (HAL_TIM_PWM_Start(ch->htim, ch->channel) == HAL_OK);
}

static uint16_t output_angle_to_pulse_ticks(const output_t *h, uint8_t angle_deg)
{
	uint16_t pmin;
	uint16_t pmax;
	uint32_t pulse;

	angle_deg = output_clamp_u8(angle_deg, OUTPUT_SERVO_ANGLE_MIN_DEG, OUTPUT_SERVO_ANGLE_MAX_DEG);
	pmin = output_servo_pulse_min(h);
	pmax = output_servo_pulse_max(h);
	if (pmax <= pmin)
	{
		return pmin;
	}

	pulse = (uint32_t)pmin
	        + ((uint32_t)angle_deg * (uint32_t)(pmax - pmin))
	          / (uint32_t)OUTPUT_SERVO_ANGLE_MAX_DEG;
	return (uint16_t)pulse;
}

static uint8_t output_pulse_to_angle_deg(const output_t *h, uint16_t pulse_ticks)
{
	uint16_t pmin;
	uint16_t pmax;
	uint32_t span;
	uint32_t angle;

	pmin = output_servo_pulse_min(h);
	pmax = output_servo_pulse_max(h);
	if (pmax <= pmin)
	{
		return 0U;
	}
	if (pulse_ticks <= pmin)
	{
		return OUTPUT_SERVO_ANGLE_MIN_DEG;
	}
	if (pulse_ticks >= pmax)
	{
		return OUTPUT_SERVO_ANGLE_MAX_DEG;
	}

	span = (uint32_t)(pmax - pmin);
	angle = ((uint32_t)(pulse_ticks - pmin) * (uint32_t)OUTPUT_SERVO_ANGLE_MAX_DEG) / span;
	return (uint8_t)angle;
}

static uint16_t output_clamp_servo_pulse(const output_t *h, uint16_t pulse_ticks)
{
	uint16_t pmin;
	uint16_t pmax;

	pmin = output_servo_pulse_min(h);
	pmax = output_servo_pulse_max(h);
	if (pulse_ticks < pmin)
	{
		return pmin;
	}
	if (pulse_ticks > pmax)
	{
		return pmax;
	}
	return pulse_ticks;
}

static uint16_t output_fan_pulse_from_percent(const output_t *h, uint8_t percent)
{
	uint16_t period;

	period = output_tim_period_ticks(h->fan.htim, h->fan_period_ticks);
	if (period == 0U)
	{
		return 0U;
	}
	if (percent >= 100U)
	{
		return (uint16_t)(period - 1U);
	}
	return (uint16_t)(((uint32_t)percent * (uint32_t)period) / 100U);
}

void output_defaults(output_t *h)
{
	if (h == NULL)
	{
		return;
	}

	h->pin_heater.port = OUTPUT_DEFAULT_HEATER_PORT;
	h->pin_heater.pin = OUTPUT_DEFAULT_HEATER_PIN;
	h->pin_lamp.port = OUTPUT_DEFAULT_LAMP_PORT;
	h->pin_lamp.pin = OUTPUT_DEFAULT_LAMP_PIN;
	h->pin_mist.port = OUTPUT_DEFAULT_MIST_PORT;
	h->pin_mist.pin = OUTPUT_DEFAULT_MIST_PIN;

	h->fan.htim = NULL;
	h->fan.channel = OUTPUT_DEFAULT_FAN_CHANNEL;
	h->servo1.htim = NULL;
	h->servo1.channel = OUTPUT_DEFAULT_SERVO1_CHANNEL;
	h->servo2.htim = NULL;
	h->servo2.channel = OUTPUT_DEFAULT_SERVO2_CHANNEL;

	h->gpio_active_high = true;
	h->servo_pulse_min = 0U;
	h->servo_pulse_max = 0U;
	h->servo_period_ticks = 0U;
	h->fan_period_ticks = 0U;

	h->fan_percent = 0U;
	h->servo1_angle_deg = 0U;
	h->servo2_angle_deg = 0U;
	h->heater_on = false;
	h->lamp_on = false;
	h->mist_on = false;
	h->initialized = false;
}

bool output_init(output_t *h)
{
	if (h == NULL)
	{
		return false;
	}
	if ((h->fan.htim == NULL) || (h->servo1.htim == NULL))
	{
		return false;
	}

	/* Servo 2 dùng chung TIM với servo 1 nếu chưa gán */
	if (h->servo2.htim == NULL)
	{
		h->servo2.htim = h->servo1.htim;
	}

	(void)output_tim_period_ticks(h->servo1.htim, h->servo_period_ticks);
	(void)output_tim_period_ticks(h->fan.htim, h->fan_period_ticks);

	if (!output_pwm_start(&h->fan))
	{
		return false;
	}
	if (!output_pwm_start(&h->servo1))
	{
		return false;
	}
	if (!output_pwm_start(&h->servo2))
	{
		return false;
	}

	output_all_off(h);
	h->initialized = true;
	return true;
}

void output_all_off(output_t *h)
{
	if (h == NULL)
	{
		return;
	}

	output_heater_set(h, false);
	output_lamp_set(h, false);
	output_mist_set(h, false);
	output_fan_set_percent(h, 0U);
	output_servo1_set_angle(h, OUTPUT_SERVO_ANGLE_MIN_DEG);
	output_servo2_set_angle(h, OUTPUT_SERVO_ANGLE_MIN_DEG);
}

void output_heater_set(output_t *h, bool on)
{
	if (h == NULL)
	{
		return;
	}
	h->heater_on = on;
	output_gpio_write(&h->pin_heater, h->gpio_active_high, on);
}

void output_lamp_set(output_t *h, bool on)
{
	if (h == NULL)
	{
		return;
	}
	h->lamp_on = on;
	output_gpio_write(&h->pin_lamp, h->gpio_active_high, on);
}

void output_mist_set(output_t *h, bool on)
{
	if (h == NULL)
	{
		return;
	}
	h->mist_on = on;
	output_gpio_write(&h->pin_mist, h->gpio_active_high, on);
}

bool output_heater_get(const output_t *h)
{
	if (h == NULL)
	{
		return false;
	}
	return h->heater_on;
}

bool output_lamp_get(const output_t *h)
{
	if (h == NULL)
	{
		return false;
	}
	return h->lamp_on;
}

bool output_mist_get(const output_t *h)
{
	if (h == NULL)
	{
		return false;
	}
	return h->mist_on;
}

void output_fan_set_percent(output_t *h, uint8_t percent)
{
	uint16_t pulse;

	if (h == NULL)
	{
		return;
	}

	percent = output_clamp_u8(percent, 0U, 100U);
	h->fan_percent = percent;
	pulse = output_fan_pulse_from_percent(h, percent);
	output_pwm_set_compare(&h->fan, pulse);
}

uint8_t output_fan_get_percent(const output_t *h)
{
	if (h == NULL)
	{
		return 0U;
	}
	return h->fan_percent;
}

void output_servo1_set_angle(output_t *h, uint8_t angle_deg)
{
	uint16_t pulse;

	if (h == NULL)
	{
		return;
	}

	angle_deg = output_clamp_u8(angle_deg, OUTPUT_SERVO_ANGLE_MIN_DEG, OUTPUT_SERVO_ANGLE_MAX_DEG);
	h->servo1_angle_deg = angle_deg;
	pulse = output_angle_to_pulse_ticks(h, angle_deg);
	output_pwm_set_compare(&h->servo1, pulse);
}

void output_servo2_set_angle(output_t *h, uint8_t angle_deg)
{
	uint16_t pulse;

	if (h == NULL)
	{
		return;
	}

	angle_deg = output_clamp_u8(angle_deg, OUTPUT_SERVO_ANGLE_MIN_DEG, OUTPUT_SERVO_ANGLE_MAX_DEG);
	h->servo2_angle_deg = angle_deg;
	pulse = output_angle_to_pulse_ticks(h, angle_deg);
	output_pwm_set_compare(&h->servo2, pulse);
}

void output_servo1_set_pulse_ticks(output_t *h, uint16_t pulse_ticks)
{
	if (h == NULL)
	{
		return;
	}

	pulse_ticks = output_clamp_servo_pulse(h, pulse_ticks);
	h->servo1_angle_deg = output_pulse_to_angle_deg(h, pulse_ticks);
	output_pwm_set_compare(&h->servo1, pulse_ticks);
}

void output_servo2_set_pulse_ticks(output_t *h, uint16_t pulse_ticks)
{
	if (h == NULL)
	{
		return;
	}

	pulse_ticks = output_clamp_servo_pulse(h, pulse_ticks);
	h->servo2_angle_deg = output_pulse_to_angle_deg(h, pulse_ticks);
	output_pwm_set_compare(&h->servo2, pulse_ticks);
}

uint8_t output_servo1_get_angle(const output_t *h)
{
	if (h == NULL)
	{
		return 0U;
	}
	return h->servo1_angle_deg;
}

uint8_t output_servo2_get_angle(const output_t *h)
{
	if (h == NULL)
	{
		return 0U;
	}
	return h->servo2_angle_deg;
}

bool output_apply_cmd(output_t *h, const output_cmd_t *cmd)
{
	if ((h == NULL) || (cmd == NULL))
	{
		return false;
	}

	switch (cmd->id)
	{
	case OUTPUT_CMD_NONE:
		return true;

	case OUTPUT_CMD_ALL_OFF:
		output_all_off(h);
		return true;

	case OUTPUT_CMD_HEATER_SET:
		output_heater_set(h, (cmd->value != 0));
		return true;

	case OUTPUT_CMD_LAMP_SET:
		output_lamp_set(h, (cmd->value != 0));
		return true;

	case OUTPUT_CMD_MIST_SET:
		output_mist_set(h, (cmd->value != 0));
		return true;

	case OUTPUT_CMD_FAN_SET_PERCENT:
		output_fan_set_percent(h, (uint8_t)cmd->value);
		return true;

	case OUTPUT_CMD_SERVO1_SET_ANGLE:
		output_servo1_set_angle(h, (uint8_t)cmd->value);
		return true;

	case OUTPUT_CMD_SERVO2_SET_ANGLE:
		output_servo2_set_angle(h, (uint8_t)cmd->value);
		return true;

	case OUTPUT_CMD_SERVO1_SET_PULSE:
		output_servo1_set_pulse_ticks(h, (uint16_t)cmd->value);
		return true;

	case OUTPUT_CMD_SERVO2_SET_PULSE:
		output_servo2_set_pulse_ticks(h, (uint16_t)cmd->value);
		return true;

	default:
		return false;
	}
}
