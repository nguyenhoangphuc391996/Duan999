/*
 * output_control.c
 *
 * Điều khiển tự động: nhiệt / ẩm / CO2 / đèn / quạt / SG90 theo chế độ vận hành.
 */

#include "output_control.h"
#include <string.h>

static int32_t round_div_i32(int32_t a, int32_t b)
{
	if (b == 0)
	{
		return 0;
	}
	if (a >= 0)
	{
		return (a + (b / 2)) / b;
	}
	return (a - (b / 2)) / b;
}

static bool is_time_in_window(uint8_t now_h, uint8_t now_m,
                              uint8_t start_h, uint8_t start_m,
                              uint8_t stop_h, uint8_t stop_m)
{
	uint16_t now_min = (uint16_t)now_h * 60U + (uint16_t)now_m;
	uint16_t start_min = (uint16_t)start_h * 60U + (uint16_t)start_m;
	uint16_t stop_min = (uint16_t)stop_h * 60U + (uint16_t)stop_m;

	if (start_min == stop_min)
	{
		return false;
	}
	if (start_min < stop_min)
	{
		return ((now_min >= start_min) && (now_min < stop_min));
	}
	return ((now_min >= start_min) || (now_min < stop_min));
}

void output_ctrl_state_init(output_ctrl_state_t *st)
{
	if (st == NULL)
	{
		return;
	}
	st->thanh_trung_start_tick = 0U;
	st->last_mode = MODE_COUNT;
}

void output_ctrl_snapshot_take(output_ctrl_snapshot_t *s,
                               app_menu_ctx_t *menu,
                               osMutexId_t menu_mutex)
{
	uint8_t target;
	uint8_t count;
	uint8_t valid_temp_count = 0U;
	int32_t sum_temp_deci = 0;

	if ((s == NULL) || (menu == NULL))
	{
		return;
	}

	memset(s, 0, sizeof(*s));
	osMutexAcquire(menu_mutex, osWaitForever);

	s->auto_mode_valid = (menu->active_mode < MODE_NGHI);
	s->mode = menu->active_mode;

	target = menu->ds18b20_target_count;
	count = (target == 0U) ? menu->ds18b20_count : target;
	if (count > MENU_DS18B20_MAX)
	{
		count = MENU_DS18B20_MAX;
	}

	for (uint8_t i = 0U; i < count; i++)
	{
		if ((menu->ds18b20_fault_mask & (uint8_t)(1U << i)) != 0U)
		{
			continue;
		}
		if (menu->ds18b20[i].tick == 0U)
		{
			continue;
		}
		sum_temp_deci += (int32_t)menu->ds18b20[i].tempDeciC;
		s->valid_temp = true;
		valid_temp_count++;
	}
	if (s->valid_temp && (valid_temp_count > 0U))
	{
		int32_t avg_temp_deci = sum_temp_deci / (int32_t)valid_temp_count;
		s->temp_c = (int16_t)round_div_i32(avg_temp_deci, 10);
	}

	if (!menu->scd41_fault)
	{
		s->valid_humi = true;
		s->valid_co2 = true;
		s->humi_percent = (int16_t)round_div_i32(menu->scd41.humidity_m_percent_rh, 1000);
		s->co2_ppm = menu->scd41.co2;
	}

	if (s->auto_mode_valid)
	{
		const mode_settings_t *cfg = &menu->mode_cfg[menu->active_mode];

		s->temp_min = cfg->nhiet_do.min;
		s->temp_max = cfg->nhiet_do.max;
		s->humi_min = cfg->do_am.min;
		s->humi_max = cfg->do_am.max;
		s->humi_disabled = (cfg->do_am_disabled != 0U);
		s->co2_min = cfg->co2.min;
		s->co2_max = cfg->co2.max;
		s->light_start_h = cfg->den.time_start_h;
		s->light_start_m = cfg->den.time_start_m;
		s->light_stop_h = cfg->den.time_stop_h;
		s->light_stop_m = cfg->den.time_stop_m;
		s->light_den_mode = cfg->den.den_mode;

		if (s->mode <= MODE_QUA_THE)
		{
			s->fan_percent = cfg->toc_do_quat;
		}

		s->thanh_trung_initial_minutes = cfg->thanh_trung_initial_minutes;
		s->sg90_mo_to_deg = cfg->sg90_mo_to_deg;
		s->sg90_mo_nho_deg = cfg->sg90_mo_nho_deg;
	}

	s->fan_learn_active  = (menu->fan_learn_active != 0U);
	s->fan_learn_pwm_pct = menu->fan_learn_pwm_pct;
	s->fan_force_off     = (menu->fan_force_off != 0U);
	s->now_h = menu->time_cfg.hour;
	s->now_m = menu->time_cfg.minute;
	osMutexRelease(menu_mutex);
}

bool output_ctrl_apply(output_t *h,
                       output_ctrl_state_t *st,
                       const output_ctrl_snapshot_t *s,
                       uint32_t now_tick)
{
	if ((h == NULL) || (st == NULL) || (s == NULL))
	{
		return false;
	}

	if (!s->auto_mode_valid)
	{
		return false;
	}

	if (s->mode != st->last_mode)
	{
		st->last_mode = s->mode;
		if (s->mode == MODE_THANH_TRUNG)
		{
			st->thanh_trung_start_tick = now_tick;
		}
	}

	if (s->mode == MODE_THANH_TRUNG)
	{
		output_heater_set(h, false);
		output_mist_set(h, false);
		output_lamp_set(h, false);
		output_fan_set_percent(h, 0U);
		output_servo2_set_angle(h, 0U);

		{
			uint32_t elapsed_ms = now_tick - st->thanh_trung_start_tick;
			uint32_t initial_ms = (uint32_t)s->thanh_trung_initial_minutes * 60U * 1000U;

			if (elapsed_ms < initial_ms)
			{
				output_servo1_set_angle(h, s->sg90_mo_to_deg);
			}
			else if (s->valid_temp)
			{
				if (s->temp_c > s->temp_max)
				{
					output_servo1_set_angle(h, s->sg90_mo_to_deg);
				}
				else if (s->temp_c < s->temp_min)
				{
					output_servo1_set_angle(h, s->sg90_mo_nho_deg);
				}
			}
		}
		return true;
	}

	if (s->valid_temp)
	{
		if (s->temp_c < s->temp_min)
		{
			output_heater_set(h, true);
		}
		else if (s->temp_c > s->temp_max)
		{
			output_heater_set(h, false);
		}
	}

	if (s->humi_disabled)
	{
		output_mist_set(h, false);
	}
	else if (s->valid_humi)
	{
		if (s->humi_percent < s->humi_min)
		{
			output_mist_set(h, true);
		}
		else if (s->humi_percent > s->humi_max)
		{
			output_mist_set(h, false);
		}
	}

	if (s->valid_co2)
	{
		if (s->co2_ppm > (uint16_t)s->co2_max)
		{
			output_servo1_set_angle(h, 180U);
			output_servo2_set_angle(h, 180U);
		}
		else if (s->co2_ppm < (uint16_t)s->co2_min)
		{
			output_servo1_set_angle(h, 0U);
			output_servo2_set_angle(h, 0U);
		}
	}

	if (s->light_den_mode == (uint8_t)DEN_MODE_KHONG_SU_DUNG)
	{
		output_lamp_set(h, false);
	}
	else if (s->light_den_mode == (uint8_t)DEN_MODE_24_24)
	{
		output_lamp_set(h, true);
	}
	else
	{
		output_lamp_set(h,
		                is_time_in_window(s->now_h, s->now_m,
		                                  s->light_start_h, s->light_start_m,
		                                  s->light_stop_h, s->light_stop_m));
	}

	if (s->fan_force_off)
	{
		output_fan_set_percent(h, 0U);
	}
	else if (s->fan_learn_active)
	{
		output_fan_set_percent(h, s->fan_learn_pwm_pct);
	}
	else
	{
		output_fan_set_percent(h, s->fan_percent);
	}
	return true;
}
