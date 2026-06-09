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

static uint8_t output_ep_lo(uint8_t close_deg, uint8_t open_deg)
{
	if (close_deg < open_deg)
	{
		return close_deg;
	}
	return open_deg;
}

static uint8_t output_ep_hi(uint8_t close_deg, uint8_t open_deg)
{
	if (close_deg > open_deg)
	{
		return close_deg;
	}
	return open_deg;
}

static uint8_t output_clamp_servo_ep(uint8_t close_deg, uint8_t open_deg, uint8_t angle_deg)
{
	return output_clamp_u8(angle_deg, output_ep_lo(close_deg, open_deg),
	                       output_ep_hi(close_deg, open_deg));
}

static void output_pwm_set_compare(output_pwm_ch_t *ch, uint16_t pulse_ticks);
/** Góc tuyệt đối 0..180 (cùng thang LCD khi học) → xung PWM. */
static uint16_t output_angle_to_pulse_ticks(const output_t *h, uint8_t angle_deg);
static uint8_t output_pulse_to_angle_deg(const output_t *h, uint16_t pulse_ticks);
static void output_servo_apply_permille(output_t *h, uint8_t servo_idx,
                                        output_pwm_ch_t *ch, uint8_t *angle_deg,
                                        int16_t permille);

static uint8_t output_servo_close_ep(const output_t *h, uint8_t servo_idx)
{
	if (servo_idx == 0U)
	{
		return h->servo1_close_deg;
	}
	return h->servo2_close_deg;
}

static uint8_t output_servo_open_ep(const output_t *h, uint8_t servo_idx)
{
	if (servo_idx == 0U)
	{
		return h->servo1_open_deg;
	}
	return h->servo2_open_deg;
}

void output_servo_apply_cal(output_t *h, const output_servo_cal_t *cal)
{
	bool changed;

	if ((h == NULL) || (cal == NULL))
	{
		return;
	}

	changed = (h->servo1_close_deg != cal->servo1_close_deg)
	          || (h->servo1_open_deg != cal->servo1_open_deg)
	          || (h->servo2_close_deg != cal->servo2_close_deg)
	          || (h->servo2_open_deg != cal->servo2_open_deg);

	h->servo1_close_deg = cal->servo1_close_deg;
	h->servo1_open_deg  = cal->servo1_open_deg;
	h->servo2_close_deg = cal->servo2_close_deg;
	h->servo2_open_deg  = cal->servo2_open_deg;

	/* Cùng permille nhưng đóng/mở đổi → góc PWM phải tính lại (sau reset / load Flash). */
	if (changed && h->initialized)
	{
		output_servo_apply_permille(h, 0U, &h->servo1, &h->servo1_angle_deg, h->servo1_permille);
		output_servo_apply_permille(h, 1U, &h->servo2, &h->servo2_angle_deg, h->servo2_permille);
	}
}

uint8_t output_servo_close_deg(const output_t *h, uint8_t servo_idx)
{
	if (h == NULL)
	{
		return OUTPUT_SERVO_ANGLE_MIN_DEG;
	}
	return output_servo_close_ep(h, servo_idx);
}

uint8_t output_servo_open_deg(const output_t *h, uint8_t servo_idx)
{
	if (h == NULL)
	{
		return OUTPUT_SERVO_ANGLE_MAX_DEG;
	}
	return output_servo_open_ep(h, servo_idx);
}

uint8_t output_servo_angle_from_percent(const output_t *h, uint8_t servo_idx, uint8_t percent)
{
	uint8_t close_deg;
	uint8_t open_deg;
	int32_t span;
	int32_t angle;

	if (h == NULL)
	{
		return 0U;
	}
	if (percent > 100U)
	{
		percent = 100U;
	}

	close_deg = output_servo_close_ep(h, servo_idx);
	open_deg  = output_servo_open_ep(h, servo_idx);
	span = (int32_t)open_deg - (int32_t)close_deg;
	angle = (int32_t)close_deg + (span * (int32_t)percent) / 100;
	return output_clamp_servo_ep(close_deg, open_deg, (uint8_t)angle);
}

static int16_t output_servo_angle_to_permille(uint8_t close_deg, uint8_t open_deg,
                                              uint8_t angle_deg)
{
	int32_t span;
	int32_t permille;

	span = (int32_t)open_deg - (int32_t)close_deg;
	if (span == 0)
	{
		return 0;
	}

	permille = ((int32_t)angle_deg - (int32_t)close_deg) * 1000 / span;
	if (permille < 0)
	{
		permille = 0;
	}
	if (permille > 1000)
	{
		permille = 1000;
	}
	return (int16_t)permille;
}

static uint8_t output_permille_to_angle(uint8_t close_deg, uint8_t open_deg, int16_t permille)
{
	int32_t span;
	int32_t angle;

	if (permille < 0)
	{
		permille = 0;
	}
	if (permille > 1000)
	{
		permille = 1000;
	}

	span = (int32_t)open_deg - (int32_t)close_deg;
	angle = (int32_t)close_deg + (span * (int32_t)permille) / 1000;
	return output_clamp_servo_ep(close_deg, open_deg, (uint8_t)angle);
}

static void output_servo_apply_permille(output_t *h, uint8_t servo_idx,
                                        output_pwm_ch_t *ch, uint8_t *angle_deg,
                                        int16_t permille)
{
	uint8_t close_deg;
	uint8_t open_deg;
	uint8_t angle;
	uint16_t pulse;

	close_deg = output_servo_close_ep(h, servo_idx);
	open_deg  = output_servo_open_ep(h, servo_idx);
	angle = output_permille_to_angle(close_deg, open_deg, permille);
	*angle_deg = angle;
	pulse = output_angle_to_pulse_ticks(h, angle);
	output_pwm_set_compare(ch, pulse);
}

static void output_servo_set_target_permille(int16_t *target_permille,
                                               uint8_t close_deg, uint8_t open_deg,
                                               uint8_t angle_deg)
{
	angle_deg = output_clamp_servo_ep(close_deg, open_deg, angle_deg);
	*target_permille = output_servo_angle_to_permille(close_deg, open_deg, angle_deg);
}

static void output_servo_ramp_step(int16_t *current, int16_t target, int16_t step)
{
	if (*current < target)
	{
		*current += step;
		if (*current > target)
		{
			*current = target;
		}
	}
	else if (*current > target)
	{
		*current -= step;
		if (*current < target)
		{
			*current = target;
		}
	}
}

static void output_servo_sync_present(output_t *h)
{
	h->servo1_permille = h->servo1_target_permille;
	h->servo2_permille = h->servo2_target_permille;
	output_servo_apply_permille(h, 0U, &h->servo1, &h->servo1_angle_deg, h->servo1_permille);
	output_servo_apply_permille(h, 1U, &h->servo2, &h->servo2_angle_deg, h->servo2_permille);
}

void output_servo_ramp_update(output_t *h, uint32_t now_tick)
{
	uint32_t elapsed_ms;
	int16_t step;
	bool servo1_moved = false;
	bool servo2_moved = false;

	if ((h == NULL) || !h->initialized)
	{
		return;
	}

	if (h->servo_ramp_last_tick == 0U)
	{
		h->servo_ramp_last_tick = now_tick;
		return;
	}

	elapsed_ms = now_tick - h->servo_ramp_last_tick;
	h->servo_ramp_last_tick = now_tick;
	if (elapsed_ms == 0U)
	{
		return;
	}
	if (elapsed_ms > 200U)
	{
		elapsed_ms = 200U;
	}

	step = (int16_t)((1000U * elapsed_ms) / OUTPUT_SERVO_RAMP_FULL_MS);
	if (step < 1)
	{
		step = 1;
	}

	if (h->servo1_permille != h->servo1_target_permille)
	{
		output_servo_ramp_step(&h->servo1_permille, h->servo1_target_permille, step);
		servo1_moved = true;
	}
	if (h->servo2_permille != h->servo2_target_permille)
	{
		output_servo_ramp_step(&h->servo2_permille, h->servo2_target_permille, step);
		servo2_moved = true;
	}

	if (servo1_moved)
	{
		output_servo_apply_permille(h, 0U, &h->servo1, &h->servo1_angle_deg, h->servo1_permille);
	}
	if (servo2_moved)
	{
		output_servo_apply_permille(h, 1U, &h->servo2, &h->servo2_angle_deg, h->servo2_permille);
	}
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
	int32_t span_deg;
	int32_t pulse;

	if (angle_deg > OUTPUT_SERVO_LEARN_RAW_OPEN_DEG)
	{
		angle_deg = OUTPUT_SERVO_LEARN_RAW_OPEN_DEG;
	}

	pmin = output_servo_pulse_min(h);
	pmax = output_servo_pulse_max(h);
	if (pmax <= pmin)
	{
		return pmin;
	}

	span_deg = (int32_t)OUTPUT_SERVO_LEARN_RAW_OPEN_DEG
	           - (int32_t)OUTPUT_SERVO_LEARN_RAW_CLOSE_DEG;
	if (span_deg == 0)
	{
		return pmin;
	}

	pulse = (int32_t)pmin
	        + ((int32_t)(pmax - pmin)
	           * ((int32_t)angle_deg - (int32_t)OUTPUT_SERVO_LEARN_RAW_CLOSE_DEG))
	          / span_deg;
	if (pulse < (int32_t)pmin)
	{
		return pmin;
	}
	if (pulse > (int32_t)pmax)
	{
		return pmax;
	}
	return (uint16_t)pulse;
}

static uint8_t output_pulse_to_angle_deg(const output_t *h, uint16_t pulse_ticks)
{
	uint16_t pmin;
	uint16_t pmax;
	int32_t span_deg;
	int32_t span_pulse;
	int32_t angle;

	pmin = output_servo_pulse_min(h);
	pmax = output_servo_pulse_max(h);
	if (pmax <= pmin)
	{
		return OUTPUT_SERVO_LEARN_RAW_CLOSE_DEG;
	}
	if (pulse_ticks <= pmin)
	{
		return OUTPUT_SERVO_LEARN_RAW_CLOSE_DEG;
	}
	if (pulse_ticks >= pmax)
	{
		return OUTPUT_SERVO_LEARN_RAW_OPEN_DEG;
	}

	span_deg = (int32_t)OUTPUT_SERVO_LEARN_RAW_OPEN_DEG
	           - (int32_t)OUTPUT_SERVO_LEARN_RAW_CLOSE_DEG;
	span_pulse = (int32_t)pmax - (int32_t)pmin;
	if ((span_deg == 0) || (span_pulse == 0))
	{
		return OUTPUT_SERVO_LEARN_RAW_CLOSE_DEG;
	}

	angle = (int32_t)OUTPUT_SERVO_LEARN_RAW_CLOSE_DEG
	        + (span_deg * ((int32_t)pulse_ticks - (int32_t)pmin)) / span_pulse;
	if (angle < 0)
	{
		angle = 0;
	}
	if (angle > (int32_t)OUTPUT_SERVO_LEARN_RAW_OPEN_DEG)
	{
		angle = (int32_t)OUTPUT_SERVO_LEARN_RAW_OPEN_DEG;
	}
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
	h->servo1_permille = 0;
	h->servo2_permille = 0;
	h->servo1_target_permille = 0;
	h->servo2_target_permille = 0;
	h->servo_ramp_last_tick = 0U;
	h->servo1_close_deg = OUTPUT_SERVO_ANGLE_MIN_DEG;
	h->servo1_open_deg  = OUTPUT_SERVO_ANGLE_MAX_DEG;
	h->servo2_close_deg = OUTPUT_SERVO_ANGLE_MIN_DEG;
	h->servo2_open_deg  = OUTPUT_SERVO_ANGLE_MAX_DEG;
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
	output_servo_sync_present(h);
	h->servo_ramp_last_tick = 0U;
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

	output_servo_set_target_permille(&h->servo1_target_permille,
	                                 h->servo1_close_deg, h->servo1_open_deg,
	                                 h->servo1_close_deg);
	output_servo_set_target_permille(&h->servo2_target_permille,
	                                 h->servo2_close_deg, h->servo2_open_deg,
	                                 h->servo2_close_deg);
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

void output_servo_set_angle_live(output_t *h, uint8_t servo_num, uint8_t angle_deg)
{
	uint8_t close_deg;
	uint8_t open_deg;
	int16_t permille;

	if (h == NULL)
	{
		return;
	}

	if (servo_num == 1U)
	{
		close_deg = h->servo1_close_deg;
		open_deg  = h->servo1_open_deg;
		angle_deg = output_clamp_servo_ep(close_deg, open_deg, angle_deg);
		permille = output_servo_angle_to_permille(close_deg, open_deg, angle_deg);
		h->servo1_target_permille = permille;
		h->servo1_permille = permille;
		output_servo_apply_permille(h, 0U, &h->servo1, &h->servo1_angle_deg, permille);
	}
	else if (servo_num == 2U)
	{
		close_deg = h->servo2_close_deg;
		open_deg  = h->servo2_open_deg;
		angle_deg = output_clamp_servo_ep(close_deg, open_deg, angle_deg);
		permille = output_servo_angle_to_permille(close_deg, open_deg, angle_deg);
		h->servo2_target_permille = permille;
		h->servo2_permille = permille;
		output_servo_apply_permille(h, 1U, &h->servo2, &h->servo2_angle_deg, permille);
	}
}

void output_servo_set_learn_preview(output_t *h, uint8_t servo_num, uint8_t angle_deg)
{
	uint16_t pulse;

	if (h == NULL)
	{
		return;
	}

	if (angle_deg > OUTPUT_SERVO_LEARN_RAW_OPEN_DEG)
	{
		angle_deg = OUTPUT_SERVO_LEARN_RAW_OPEN_DEG;
	}

	pulse = output_angle_to_pulse_ticks(h, angle_deg);

	if (servo_num == 1U)
	{
		h->servo1_angle_deg = angle_deg;
		output_pwm_set_compare(&h->servo1, pulse);
	}
	else if (servo_num == 2U)
	{
		h->servo2_angle_deg = angle_deg;
		output_pwm_set_compare(&h->servo2, pulse);
	}
}

static void output_servo_resync_channel(output_t *h, uint8_t servo_idx)
{
	uint16_t pulse;
	uint8_t close_deg;
	uint8_t open_deg;
	uint8_t raw_angle;
	uint8_t cal_angle;
	int16_t permille;
	output_pwm_ch_t *ch;
	int16_t *permille_cur;
	int16_t *permille_tgt;
	uint8_t *angle_deg;

	if ((h == NULL) || !h->initialized)
	{
		return;
	}

	if (servo_idx == 0U)
	{
		ch = &h->servo1;
		close_deg = h->servo1_close_deg;
		open_deg = h->servo1_open_deg;
		permille_cur = &h->servo1_permille;
		permille_tgt = &h->servo1_target_permille;
		angle_deg = &h->servo1_angle_deg;
	}
	else
	{
		ch = &h->servo2;
		close_deg = h->servo2_close_deg;
		open_deg = h->servo2_open_deg;
		permille_cur = &h->servo2_permille;
		permille_tgt = &h->servo2_target_permille;
		angle_deg = &h->servo2_angle_deg;
	}

	if ((ch->htim == NULL) || (ch->htim->Instance == NULL))
	{
		return;
	}

	pulse = (uint16_t)__HAL_TIM_GET_COMPARE(ch->htim, ch->channel);
	raw_angle = output_pulse_to_angle_deg(h, pulse);
	cal_angle = output_clamp_servo_ep(close_deg, open_deg, raw_angle);
	permille = output_servo_angle_to_permille(close_deg, open_deg, cal_angle);
	*permille_cur = permille;
	*permille_tgt = permille;
	*angle_deg = cal_angle;
}

void output_servo_resync_all(output_t *h)
{
	output_servo_resync_channel(h, 0U);
	output_servo_resync_channel(h, 1U);
}

void output_servo1_set_angle(output_t *h, uint8_t angle_deg)
{
	if (h == NULL)
	{
		return;
	}

	output_servo_set_target_permille(&h->servo1_target_permille,
	                                 h->servo1_close_deg, h->servo1_open_deg,
	                                 angle_deg);
}

void output_servo2_set_angle(output_t *h, uint8_t angle_deg)
{
	if (h == NULL)
	{
		return;
	}

	output_servo_set_target_permille(&h->servo2_target_permille,
	                                 h->servo2_close_deg, h->servo2_open_deg,
	                                 angle_deg);
}

void output_servo1_set_pulse_ticks(output_t *h, uint16_t pulse_ticks)
{
	uint8_t angle_deg;

	if (h == NULL)
	{
		return;
	}

	pulse_ticks = output_clamp_servo_pulse(h, pulse_ticks);
	angle_deg = output_pulse_to_angle_deg(h, pulse_ticks);
	output_servo_set_target_permille(&h->servo1_target_permille,
	                                 h->servo1_close_deg, h->servo1_open_deg,
	                                 angle_deg);
}

void output_servo2_set_pulse_ticks(output_t *h, uint16_t pulse_ticks)
{
	uint8_t angle_deg;

	if (h == NULL)
	{
		return;
	}

	pulse_ticks = output_clamp_servo_pulse(h, pulse_ticks);
	angle_deg = output_pulse_to_angle_deg(h, pulse_ticks);
	output_servo_set_target_permille(&h->servo2_target_permille,
	                                 h->servo2_close_deg, h->servo2_open_deg,
	                                 angle_deg);
}

uint8_t output_servo1_get_angle(const output_t *h)
{
	if (h == NULL)
	{
		return 0U;
	}
	return output_permille_to_angle(h->servo1_close_deg, h->servo1_open_deg,
	                                h->servo1_permille);
}

uint8_t output_servo2_get_angle(const output_t *h)
{
	if (h == NULL)
	{
		return 0U;
	}
	return output_permille_to_angle(h->servo2_close_deg, h->servo2_open_deg,
	                                h->servo2_permille);
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
