/*
 * output_control.h
 *
 * Logic điều khiển tự động đầu ra theo chế độ + cảm biến (snapshot từ menu).
 */

#ifndef OUTPUT_CONTROL_H_
#define OUTPUT_CONTROL_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "cmsis_os.h"
#include "app_menu.h"
#include "output.h"

/** @brief Snapshot dữ liệu điều khiển (đọc từ menu context). */
typedef struct
{
	bool auto_mode_valid;
	app_mode_t mode;
	bool valid_temp;
	int16_t temp_c;
	bool valid_humi;
	int16_t humi_percent;
	bool valid_co2;
	uint16_t co2_ppm;
	int16_t temp_min;
	int16_t temp_max;
	int16_t humi_min;
	int16_t humi_max;
	bool humi_disabled;
	int16_t co2_min;
	int16_t co2_max;
	uint8_t light_start_h;
	uint8_t light_start_m;
	uint8_t light_stop_h;
	uint8_t light_stop_m;
	uint8_t light_den_mode;
	uint8_t fan_percent;
	bool    fan_learn_active;    /**< true = learning task đang override quạt */
	uint8_t fan_learn_pwm_pct;  /**< PWM % do learning task yêu cầu          */
	uint8_t now_h;
	uint8_t now_m;
	uint16_t thanh_trung_initial_minutes;
	uint8_t sg90_mo_to_deg;
	uint8_t sg90_mo_nho_deg;
} output_ctrl_snapshot_t;

/** @brief Trạng thái nội bộ vòng điều khiển (thanh trùng, chế độ trước). */
typedef struct
{
	uint32_t thanh_trung_start_tick;
	app_mode_t last_mode;
} output_ctrl_state_t;

void output_ctrl_state_init(output_ctrl_state_t *st);

/**
 * @brief Lấy snapshot từ menu (có khóa mutex).
 */
void output_ctrl_snapshot_take(output_ctrl_snapshot_t *s,
                               app_menu_ctx_t *menu,
                               osMutexId_t menu_mutex);

/**
 * @brief Áp dụng điều khiển tự động lên @p h theo snapshot.
 * @return false nếu chế độ Nghỉ (caller nên gọi output_all_off).
 */
bool output_ctrl_apply(output_t *h,
                       output_ctrl_state_t *st,
                       const output_ctrl_snapshot_t *s,
                       uint32_t now_tick);

#ifdef __cplusplus
}
#endif

#endif /* OUTPUT_CONTROL_H_ */
