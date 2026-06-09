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
	servo_defaults(&h->servo);

	h->gpio_active_high = true;
	h->fan_period_ticks = 0U;

	h->fan_percent = 0U;
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
	if ((h->fan.htim == NULL) || !h->servo.initialized)
	{
		return false;
	}

	(void)output_tim_period_ticks(h->fan.htim, h->fan_period_ticks);

	if (!output_pwm_start(&h->fan))
	{
		return false;
	}

	output_all_off(h);
	servo_sync_present(&h->servo);
	h->servo.ramp_last_tick = 0U;
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
	servo_all_off(&h->servo);
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
		servo_set_angle(&h->servo, 0U, (uint8_t)cmd->value);
		return true;

	case OUTPUT_CMD_SERVO2_SET_ANGLE:
		servo_set_angle(&h->servo, 1U, (uint8_t)cmd->value);
		return true;

	case OUTPUT_CMD_SERVO1_SET_PULSE:
		servo_set_pulse_ticks(&h->servo, 0U, (uint16_t)cmd->value);
		return true;

	case OUTPUT_CMD_SERVO2_SET_PULSE:
		servo_set_pulse_ticks(&h->servo, 1U, (uint16_t)cmd->value);
		return true;

	default:
		return false;
	}
}
