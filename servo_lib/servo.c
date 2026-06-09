/*
 * servo.c
 *
 * Điều khiển N servo SG90: config + init, cal, ramp, học góc.
 */

#include "servo.h"

static bool servo_idx_valid(const servo_t *h, uint8_t idx)
{
	return ((h != NULL) && (idx < h->count));
}

static uint8_t servo_clamp_u8(uint8_t value, uint8_t min_v, uint8_t max_v)
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

static uint8_t servo_ep_lo(uint8_t close_deg, uint8_t open_deg)
{
	if (close_deg < open_deg)
	{
		return close_deg;
	}
	return open_deg;
}

static uint8_t servo_ep_hi(uint8_t close_deg, uint8_t open_deg)
{
	if (close_deg > open_deg)
	{
		return close_deg;
	}
	return open_deg;
}

static uint8_t servo_clamp_ep(uint8_t close_deg, uint8_t open_deg, uint8_t angle_deg)
{
	return servo_clamp_u8(angle_deg, servo_ep_lo(close_deg, open_deg),
	                      servo_ep_hi(close_deg, open_deg));
}

static void servo_pwm_set_compare(const servo_pwm_ch_t *ch, uint16_t pulse_ticks);
static uint16_t servo_angle_to_pulse_ticks(const servo_t *h, uint8_t angle_deg);
static uint8_t servo_pulse_to_angle_deg(const servo_t *h, uint16_t pulse_ticks);
static void servo_apply_permille(servo_t *h, uint8_t idx, int16_t permille);

static int16_t servo_angle_to_permille(uint8_t close_deg, uint8_t open_deg,
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

static uint8_t servo_permille_to_angle(uint8_t close_deg, uint8_t open_deg, int16_t permille)
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
	return servo_clamp_ep(close_deg, open_deg, (uint8_t)angle);
}

static void servo_set_target_permille(int16_t *target_permille,
                                      uint8_t close_deg, uint8_t open_deg,
                                      uint8_t angle_deg)
{
	angle_deg = servo_clamp_ep(close_deg, open_deg, angle_deg);
	*target_permille = servo_angle_to_permille(close_deg, open_deg, angle_deg);
}

static void servo_ramp_step(int16_t *current, int16_t target, int16_t step)
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

static uint16_t servo_pulse_min(const servo_t *h)
{
	if (h->pulse_min != 0U)
	{
		return h->pulse_min;
	}
	return SERVO_PULSE_MIN_TICKS;
}

static uint16_t servo_pulse_max(const servo_t *h)
{
	if (h->pulse_max != 0U)
	{
		return h->pulse_max;
	}
	return SERVO_PULSE_MAX_TICKS;
}

static void servo_pwm_set_compare(const servo_pwm_ch_t *ch, uint16_t pulse_ticks)
{
	if ((ch == NULL) || (ch->htim == NULL) || (ch->htim->Instance == NULL))
	{
		return;
	}
	__HAL_TIM_SET_COMPARE(ch->htim, ch->channel, pulse_ticks);
}

static bool servo_pwm_start_channel(const servo_pwm_ch_t *ch)
{
	if ((ch == NULL) || (ch->htim == NULL))
	{
		return false;
	}
	return (HAL_TIM_PWM_Start(ch->htim, ch->channel) == HAL_OK);
}

static uint16_t servo_angle_to_pulse_ticks(const servo_t *h, uint8_t angle_deg)
{
	uint16_t pmin;
	uint16_t pmax;
	int32_t span_deg;
	int32_t pulse;

	if (angle_deg > SERVO_LEARN_RAW_OPEN_DEG)
	{
		angle_deg = SERVO_LEARN_RAW_OPEN_DEG;
	}

	pmin = servo_pulse_min(h);
	pmax = servo_pulse_max(h);
	if (pmax <= pmin)
	{
		return pmin;
	}

	span_deg = (int32_t)SERVO_LEARN_RAW_OPEN_DEG - (int32_t)SERVO_LEARN_RAW_CLOSE_DEG;
	if (span_deg == 0)
	{
		return pmin;
	}

	pulse = (int32_t)pmin
	        + ((int32_t)(pmax - pmin)
	           * ((int32_t)angle_deg - (int32_t)SERVO_LEARN_RAW_CLOSE_DEG))
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

static uint8_t servo_pulse_to_angle_deg(const servo_t *h, uint16_t pulse_ticks)
{
	uint16_t pmin;
	uint16_t pmax;
	int32_t span_deg;
	int32_t span_pulse;
	int32_t angle;

	pmin = servo_pulse_min(h);
	pmax = servo_pulse_max(h);
	if (pmax <= pmin)
	{
		return SERVO_LEARN_RAW_CLOSE_DEG;
	}
	if (pulse_ticks <= pmin)
	{
		return SERVO_LEARN_RAW_CLOSE_DEG;
	}
	if (pulse_ticks >= pmax)
	{
		return SERVO_LEARN_RAW_OPEN_DEG;
	}

	span_deg = (int32_t)SERVO_LEARN_RAW_OPEN_DEG - (int32_t)SERVO_LEARN_RAW_CLOSE_DEG;
	span_pulse = (int32_t)pmax - (int32_t)pmin;
	if ((span_deg == 0) || (span_pulse == 0))
	{
		return SERVO_LEARN_RAW_CLOSE_DEG;
	}

	angle = (int32_t)SERVO_LEARN_RAW_CLOSE_DEG
	        + (span_deg * ((int32_t)pulse_ticks - (int32_t)pmin)) / span_pulse;
	if (angle < 0)
	{
		angle = 0;
	}
	if (angle > (int32_t)SERVO_LEARN_RAW_OPEN_DEG)
	{
		angle = (int32_t)SERVO_LEARN_RAW_OPEN_DEG;
	}
	return (uint8_t)angle;
}

static uint16_t servo_clamp_pulse(const servo_t *h, uint16_t pulse_ticks)
{
	uint16_t pmin;
	uint16_t pmax;

	pmin = servo_pulse_min(h);
	pmax = servo_pulse_max(h);
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

static void servo_apply_permille(servo_t *h, uint8_t idx, int16_t permille)
{
	uint8_t close_deg;
	uint8_t open_deg;
	uint8_t angle;
	uint16_t pulse;

	if (!servo_idx_valid(h, idx))
	{
		return;
	}

	close_deg = h->close_deg[idx];
	open_deg  = h->open_deg[idx];
	angle = servo_permille_to_angle(close_deg, open_deg, permille);
	h->angle_deg[idx] = angle;
	pulse = servo_angle_to_pulse_ticks(h, angle);
	servo_pwm_set_compare(&h->pwm[idx], pulse);
}

static void servo_resync_channel(servo_t *h, uint8_t idx)
{
	uint16_t pulse;
	uint8_t raw_angle;
	uint8_t cal_angle;
	int16_t permille;
	const servo_pwm_ch_t *ch;

	if (!servo_idx_valid(h, idx) || !h->initialized)
	{
		return;
	}

	ch = &h->pwm[idx];
	if ((ch->htim == NULL) || (ch->htim->Instance == NULL))
	{
		return;
	}

	pulse = (uint16_t)__HAL_TIM_GET_COMPARE(ch->htim, ch->channel);
	raw_angle = servo_pulse_to_angle_deg(h, pulse);
	cal_angle = servo_clamp_ep(h->close_deg[idx], h->open_deg[idx], raw_angle);
	permille = servo_angle_to_permille(h->close_deg[idx], h->open_deg[idx], cal_angle);
	h->permille[idx] = permille;
	h->target_permille[idx] = permille;
	h->angle_deg[idx] = cal_angle;
}

void servo_defaults(servo_t *h)
{
	uint8_t i;

	if (h == NULL)
	{
		return;
	}

	h->count = 0U;
	for (i = 0U; i < SERVO_MAX_COUNT; i++)
	{
		h->pwm[i].htim = NULL;
		h->pwm[i].channel = 0U;
		h->close_deg[i] = SERVO_ANGLE_MIN_DEG;
		h->open_deg[i]  = SERVO_ANGLE_MAX_DEG;
		h->angle_deg[i] = 0U;
		h->permille[i] = 0;
		h->target_permille[i] = 0;
	}

	h->pulse_min = 0U;
	h->pulse_max = 0U;
	h->ramp_last_tick = 0U;
	h->initialized = false;
}

bool servo_init(servo_t *h, const servo_config_t *cfg)
{
	uint8_t i;

	if ((h == NULL) || (cfg == NULL) || (cfg->count == 0U) || (cfg->count > SERVO_MAX_COUNT))
	{
		return false;
	}

	servo_defaults(h);

	h->count = cfg->count;
	h->pulse_min = cfg->pulse_min;
	h->pulse_max = cfg->pulse_max;

	for (i = 0U; i < cfg->count; i++)
	{
		h->pwm[i] = cfg->ch[i];
		if (h->pwm[i].htim == NULL)
		{
			return false;
		}
		if (!servo_pwm_start_channel(&h->pwm[i]))
		{
			return false;
		}
	}

	h->initialized = true;
	return true;
}

void servo_all_off(servo_t *h)
{
	uint8_t i;

	if (h == NULL)
	{
		return;
	}

	for (i = 0U; i < h->count; i++)
	{
		servo_set_target_permille(&h->target_permille[i],
		                          h->close_deg[i], h->open_deg[i],
		                          h->close_deg[i]);
	}
}

void servo_sync_present(servo_t *h)
{
	uint8_t i;

	if (h == NULL)
	{
		return;
	}

	for (i = 0U; i < h->count; i++)
	{
		h->permille[i] = h->target_permille[i];
		servo_apply_permille(h, i, h->permille[i]);
	}
}

void servo_apply_cal(servo_t *h, const servo_cal_t *cal)
{
	bool changed;
	uint8_t i;
	uint8_t n;

	if ((h == NULL) || (cal == NULL))
	{
		return;
	}

	n = h->count;
	if (n == 0U)
	{
		n = SERVO_COUNT;
	}
	if (n > SERVO_MAX_COUNT)
	{
		n = SERVO_MAX_COUNT;
	}

	changed = false;
	for (i = 0U; i < n; i++)
	{
		if ((h->close_deg[i] != cal->close_deg[i])
		    || (h->open_deg[i] != cal->open_deg[i]))
		{
			changed = true;
		}
		h->close_deg[i] = cal->close_deg[i];
		h->open_deg[i]  = cal->open_deg[i];
	}

	if (changed && h->initialized)
	{
		for (i = 0U; i < h->count; i++)
		{
			servo_apply_permille(h, i, h->permille[i]);
		}
	}
}

uint8_t servo_close_deg(const servo_t *h, uint8_t idx)
{
	if (!servo_idx_valid(h, idx))
	{
		return SERVO_ANGLE_MIN_DEG;
	}
	return h->close_deg[idx];
}

uint8_t servo_open_deg(const servo_t *h, uint8_t idx)
{
	if (!servo_idx_valid(h, idx))
	{
		return SERVO_ANGLE_MAX_DEG;
	}
	return h->open_deg[idx];
}

uint8_t servo_angle_from_percent(const servo_t *h, uint8_t idx, uint8_t percent)
{
	uint8_t close_deg;
	uint8_t open_deg;
	int32_t span;
	int32_t angle;

	if (!servo_idx_valid(h, idx))
	{
		return 0U;
	}
	if (percent > 100U)
	{
		percent = 100U;
	}

	close_deg = h->close_deg[idx];
	open_deg  = h->open_deg[idx];
	span = (int32_t)open_deg - (int32_t)close_deg;
	angle = (int32_t)close_deg + (span * (int32_t)percent) / 100;
	return servo_clamp_ep(close_deg, open_deg, (uint8_t)angle);
}

void servo_ramp_update(servo_t *h, uint32_t now_tick)
{
	uint32_t elapsed_ms;
	int16_t step;
	uint8_t i;

	if ((h == NULL) || !h->initialized)
	{
		return;
	}

	if (h->ramp_last_tick == 0U)
	{
		h->ramp_last_tick = now_tick;
		return;
	}

	elapsed_ms = now_tick - h->ramp_last_tick;
	h->ramp_last_tick = now_tick;
	if (elapsed_ms == 0U)
	{
		return;
	}
	if (elapsed_ms > 200U)
	{
		elapsed_ms = 200U;
	}

	step = (int16_t)((1000U * elapsed_ms) / SERVO_RAMP_FULL_MS);
	if (step < 1)
	{
		step = 1;
	}

	for (i = 0U; i < h->count; i++)
	{
		if (h->permille[i] != h->target_permille[i])
		{
			servo_ramp_step(&h->permille[i], h->target_permille[i], step);
			servo_apply_permille(h, i, h->permille[i]);
		}
	}
}

void servo_set_angle(servo_t *h, uint8_t idx, uint8_t angle_deg)
{
	if (!servo_idx_valid(h, idx))
	{
		return;
	}

	servo_set_target_permille(&h->target_permille[idx],
	                          h->close_deg[idx], h->open_deg[idx],
	                          angle_deg);
}

void servo_set_angle_live(servo_t *h, uint8_t idx, uint8_t angle_deg)
{
	uint8_t close_deg;
	uint8_t open_deg;
	int16_t permille;

	if (!servo_idx_valid(h, idx))
	{
		return;
	}

	close_deg = h->close_deg[idx];
	open_deg  = h->open_deg[idx];
	angle_deg = servo_clamp_ep(close_deg, open_deg, angle_deg);
	permille = servo_angle_to_permille(close_deg, open_deg, angle_deg);
	h->target_permille[idx] = permille;
	h->permille[idx] = permille;
	servo_apply_permille(h, idx, permille);
}

void servo_set_learn_preview(servo_t *h, uint8_t idx, uint8_t angle_deg)
{
	uint16_t pulse;

	if (!servo_idx_valid(h, idx))
	{
		return;
	}

	if (angle_deg > SERVO_LEARN_RAW_OPEN_DEG)
	{
		angle_deg = SERVO_LEARN_RAW_OPEN_DEG;
	}

	pulse = servo_angle_to_pulse_ticks(h, angle_deg);
	h->angle_deg[idx] = angle_deg;
	servo_pwm_set_compare(&h->pwm[idx], pulse);
}

void servo_set_pulse_ticks(servo_t *h, uint8_t idx, uint16_t pulse_ticks)
{
	uint8_t angle_deg;

	if (!servo_idx_valid(h, idx))
	{
		return;
	}

	pulse_ticks = servo_clamp_pulse(h, pulse_ticks);
	angle_deg = servo_pulse_to_angle_deg(h, pulse_ticks);
	servo_set_target_permille(&h->target_permille[idx],
	                          h->close_deg[idx], h->open_deg[idx],
	                          angle_deg);
}

uint8_t servo_get_angle(const servo_t *h, uint8_t idx)
{
	if (!servo_idx_valid(h, idx))
	{
		return 0U;
	}
	return servo_permille_to_angle(h->close_deg[idx], h->open_deg[idx],
	                               h->permille[idx]);
}

void servo_resync_all(servo_t *h)
{
	uint8_t i;

	if (h == NULL)
	{
		return;
	}

	for (i = 0U; i < h->count; i++)
	{
		servo_resync_channel(h, i);
	}
}
