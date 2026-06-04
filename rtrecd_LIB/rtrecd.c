/*
 * rtrecd.c
 *
 *  Created on: 16 thg 3, 2026
 *      Author: embedded
 */

#include "rtrecd.h"
#include "cmsis_os.h"

static bool rtrecd_pin_is_active(const rtrecd_gpio_t *gpio, bool active_low)
{
	GPIO_PinState raw_state;

	if ((gpio == NULL) || (gpio->port == NULL))
	{
		return false;
	}

	raw_state = HAL_GPIO_ReadPin(gpio->port, gpio->pin);
	if (active_low)
	{
		return (raw_state == GPIO_PIN_RESET);
	}

	return (raw_state == GPIO_PIN_SET);
}

static uint8_t rtrecd_read_ab_state(const rtrecd_t *h)
{
	uint8_t a;
	uint8_t b;

	a = rtrecd_pin_is_active(&h->pin_a, h->ab_active_low) ? 1U : 0U;
	b = rtrecd_pin_is_active(&h->pin_b, h->ab_active_low) ? 1U : 0U;

	return (uint8_t)((a << 1U) | b);
}

static void rtrecd_clear_rotation_pending(rtrecd_t *h)
{
	uint32_t primask;

	primask = __get_PRIMASK();
	__disable_irq();
	h->step_acc = 0;
	h->rot_cw_pending = 0U;
	h->rot_ccw_pending = 0U;
	if (primask == 0U)
	{
		__enable_irq();
	}
}

static void rtrecd_suppress_rotation(rtrecd_t *h, uint32_t now_ms)
{
	h->rot_suppress_until_ms = now_ms + (uint32_t)RTRECD_ROT_SUPPRESS_MS;
	rtrecd_clear_rotation_pending(h);
}

static void rtrecd_update_rotation_from_isr(rtrecd_t *h)
{
	static const int8_t transition_table[16] =
	{
		0, -1,  1,  0,
		1,  0,  0, -1,
		-1, 0,  0,  1,
		0,  1, -1,  0
	};
	uint8_t curr_state;
	uint8_t index;
	int8_t step;
	uint8_t steps_per_detent;

	/* Ấn nút SW làm lệch trục → bỏ quadrature trong lúc giữ nút. */
	if (rtrecd_pin_is_active(&h->pin_sw, h->sw_active_low))
	{
		return;
	}

	curr_state = rtrecd_read_ab_state(h);
	index = (uint8_t)((h->prev_ab_state << 2U) | curr_state);
	step = transition_table[index];
	h->prev_ab_state = curr_state;

	if (step == 0)
	{
		return;
	}

	steps_per_detent = (h->steps_per_detent == 0U) ? RTRECD_DEFAULT_STEPS_PER_DETENT : h->steps_per_detent;

	/* Discard partial steps when direction flips to avoid reverse-direction lag. */
	if (((h->step_acc > 0) && (step < 0)) || ((h->step_acc < 0) && (step > 0)))
	{
		h->step_acc = 0;
	}

	h->step_acc += step;
	while (h->step_acc >= (int8_t)steps_per_detent)
	{
		if (h->rot_cw_pending < RTRECD_ROT_PENDING_MAX)
		{
			h->rot_cw_pending += 1U;
		}
		h->step_acc -= (int8_t)steps_per_detent;
	}
	while (h->step_acc <= -(int8_t)steps_per_detent)
	{
		if (h->rot_ccw_pending < RTRECD_ROT_PENDING_MAX)
		{
			h->rot_ccw_pending += 1U;
		}
		h->step_acc += (int8_t)steps_per_detent;
	}
}

bool rtrecd_init(rtrecd_t *h)
{
	uint32_t now_ms;
	bool sw_active;
	bool use_default_active_level;

	if ((h == NULL) || (h->pin_a.port == NULL) || (h->pin_b.port == NULL) || (h->pin_sw.port == NULL))
	{
		return false;
	}

	h->initialized = false;

	if (h->get_tick_ms == NULL)
	{
		h->get_tick_ms = HAL_GetTick;
	}

	if (h->debounce_ms == 0U)
	{
		h->debounce_ms = RTRECD_DEFAULT_DEBOUNCE_MS;
	}

	if (h->long_press_ms == 0U)
	{
		h->long_press_ms = RTRECD_DEFAULT_LONG_PRESS_MS;
	}

	if (h->steps_per_detent == 0U)
	{
		h->steps_per_detent = RTRECD_DEFAULT_STEPS_PER_DETENT;
	}

	use_default_active_level = h->use_default_active_level;
	if ((h->ab_active_low == false) && (h->sw_active_low == false))
	{
		use_default_active_level = true;
	}
	if (use_default_active_level)
	{
		h->ab_active_low = true;
		h->sw_active_low = true;
	}

	h->step_acc = 0;
	h->prev_ab_state = rtrecd_read_ab_state(h);
	h->rot_cw_pending = 0U;
	h->rot_ccw_pending = 0U;

	now_ms = h->get_tick_ms();
	sw_active = rtrecd_pin_is_active(&h->pin_sw, h->sw_active_low);

	h->btn_raw_state = sw_active;
	h->btn_stable_state = sw_active;
	h->btn_pressing = sw_active;
	h->btn_long_reported = false;
	h->btn_last_bounce_ms = now_ms;
	h->btn_press_start_ms = now_ms;
	h->rot_suppress_until_ms = 0U;
	h->initialized = true;

	return true;
}

void rtrecd_isr_a(rtrecd_t *h)
{
	if ((h == NULL) || (h->initialized == false))
	{
		return;
	}

	rtrecd_update_rotation_from_isr(h);
}

void rtrecd_isr_b(rtrecd_t *h)
{
	if ((h == NULL) || (h->initialized == false))
	{
		return;
	}

	rtrecd_update_rotation_from_isr(h);
}

void rtrecd_isr_ab(rtrecd_t *h)
{
	if ((h == NULL) || (h->initialized == false))
	{
		return;
	}

	rtrecd_update_rotation_from_isr(h);
}

void rtrecd_isr_sw(rtrecd_t *h)
{
	bool sw_active;
	uint32_t now_ms;

	if ((h == NULL) || (h->initialized == false))
	{
		return;
	}

	sw_active = rtrecd_pin_is_active(&h->pin_sw, h->sw_active_low);
	now_ms = h->get_tick_ms();

	if (sw_active != h->btn_raw_state)
	{
		h->btn_raw_state = sw_active;
		h->btn_last_bounce_ms = now_ms;
	}
}

bool rtrecd_is_button_pressed(const rtrecd_t *h)
{
	if ((h == NULL) || (h->initialized == false))
	{
		return false;
	}

	return h->btn_stable_state;
}

const char *rtrecd_queue_item_to_str(rtrecd_queue_item_t event)
{
	switch (event)
	{
	case RTRECD_EVENT_NONE:
		return "NONE";
	case RTRECD_EVENT_ROTATE_CW:
		return "ROTATE_CW";
	case RTRECD_EVENT_ROTATE_CCW:
		return "ROTATE_CCW";
	case RTRECD_EVENT_BUTTON_SHORT:
		return "BUTTON_SHORT";
	case RTRECD_EVENT_BUTTON_LONG:
		return "BUTTON_LONG";
	default:
		return "UNKNOWN";
	}
}

rtrecd_queue_item_t rtrecd_process(rtrecd_t *h)
{
	rtrecd_queue_item_t out;
	rtrecd_queue_item_t rot_event;
	uint32_t now_ms;
	bool sw_active;
	rtrecd_queue_item_t button_event;
	uint32_t primask;
	uint8_t has_cw;
	uint8_t has_ccw;

	out = RTRECD_EVENT_NONE;
	rot_event = RTRECD_EVENT_NONE;
	button_event = RTRECD_EVENT_NONE;

	if ((h == NULL) || (h->get_tick_ms == NULL))
	{
		return out;
	}

	if (h->initialized == false)
	{
		return out;
	}

	now_ms = h->get_tick_ms();

	/* Atomically fetch one pending rotation event (bỏ qua trong cửa sổ suppress). */
	if (now_ms >= h->rot_suppress_until_ms)
	{
		primask = __get_PRIMASK();
		__disable_irq();
		has_cw = h->rot_cw_pending;
		has_ccw = h->rot_ccw_pending;
		if (has_cw > 0U)
		{
			h->rot_cw_pending = (uint8_t)(has_cw - 1U);
		}
		else if (has_ccw > 0U)
		{
			h->rot_ccw_pending = (uint8_t)(has_ccw - 1U);
		}
		if (primask == 0U)
		{
			__enable_irq();
		}

		if (has_cw > 0U)
		{
			rot_event = RTRECD_EVENT_ROTATE_CW;
		}
		else if (has_ccw > 0U)
		{
			rot_event = RTRECD_EVENT_ROTATE_CCW;
		}
	}

	/* btn_raw_state được cập nhật từ rtrecd_isr_sw (ngắt).
	 * Nếu không dùng ngắt SW, fallback polling vẫn hoạt động đúng. */
	sw_active = rtrecd_pin_is_active(&h->pin_sw, h->sw_active_low);

	if (sw_active != h->btn_raw_state)
	{
		h->btn_raw_state = sw_active;
		h->btn_last_bounce_ms = now_ms;
	}

	if ((uint32_t)(now_ms - h->btn_last_bounce_ms) >= h->debounce_ms)
	{
		if (h->btn_stable_state != h->btn_raw_state)
		{
			h->btn_stable_state = h->btn_raw_state;

			if (h->btn_stable_state)
			{
				h->btn_pressing = true;
				h->btn_long_reported = false;
				h->btn_press_start_ms = now_ms;
				rtrecd_suppress_rotation(h, now_ms);
			}
			else if (h->btn_pressing)
			{
				h->btn_pressing = false;
				rtrecd_suppress_rotation(h, now_ms);
				if (h->btn_long_reported == false)
				{
					button_event = RTRECD_EVENT_BUTTON_SHORT;
				}
			}
		}
	}

	if ((h->btn_pressing == true) && (h->btn_stable_state == true) && (h->btn_long_reported == false))
	{
		if ((uint32_t)(now_ms - h->btn_press_start_ms) >= h->long_press_ms)
		{
			h->btn_long_reported = true;
			button_event = RTRECD_EVENT_BUTTON_LONG;
			rtrecd_suppress_rotation(h, now_ms);
		}
	}

	/* Ưu tiên nút; không làm mất bước quay đã lấy ra nhầm trong cùng chu kỳ. */
	if (button_event != RTRECD_EVENT_NONE)
	{
		out = button_event;
	}
	else
	{
		out = rot_event;
	}

	return out;
}

bool rtrecd_service(rtrecd_t *h, osMessageQueueId_t queue)
{
	bool any_put = false;
	rtrecd_queue_item_t event;

	if ((h == NULL) || (queue == NULL))
	{
		return false;
	}

	for (;;)
	{
		event = rtrecd_process(h);
		if (event == RTRECD_EVENT_NONE)
		{
			break;
		}

		if (osMessageQueuePut(queue, &event, 0U, 0U) != osOK)
		{
			/* Queue đầy: dừng drain; pending còn lại xử lý ở chu kỳ sau. */
			break;
		}

		any_put = true;
	}

	return any_put;
}
