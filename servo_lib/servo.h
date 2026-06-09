/*
 * servo.h
 *
 * Thư viện điều khiển servo SG90 (PWM):
 *   - Cấu hình qua servo_config_t + servo_init() (giống lcd_i2c_config_t + lcd_init)
 *   - Mảng servo theo chỉ số 0..count-1 — tăng SERVO_COUNT để thêm servo
 *   - Cal, ramp, học góc, % mở
 *
 * Phụ thuộc: stm32f1xx_hal.h
 */

#ifndef SERVO_H_
#define SERVO_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "stm32f1xx_hal.h"

/* =========================================================================
 * Cấu hình compile-time
 * ========================================================================= */

/** Số kênh tối đa (bộ nhớ mảng). Tăng nếu cần > 4 servo. */
#define SERVO_MAX_COUNT           4U

/**
 * Số servo dùng trong project (<= SERVO_MAX_COUNT).
 * Đổi macro này + thêm phần tử .ch[] trong servo_config_t khi mở rộng phần cứng.
 */
#ifndef SERVO_COUNT
#define SERVO_COUNT               2U
#endif

#if (SERVO_COUNT > SERVO_MAX_COUNT)
#error SERVO_COUNT must not exceed SERVO_MAX_COUNT
#endif

/**
 * Góc đóng/mở mặc định (trước khi học / load Flash).
 * MIN < MAX: đóng→mở tăng góc. MIN > MAX: lắp ngược.
 */
#define SERVO_ANGLE_MIN_DEG_CFG   90
#define SERVO_ANGLE_MAX_DEG_CFG   160

#if (SERVO_ANGLE_MIN_DEG_CFG == SERVO_ANGLE_MAX_DEG_CFG)
#error SERVO_ANGLE_MIN_DEG_CFG and SERVO_ANGLE_MAX_DEG_CFG must differ
#endif

#define SERVO_ANGLE_MIN_DEG       ((uint8_t)SERVO_ANGLE_MIN_DEG_CFG)
#define SERVO_ANGLE_MAX_DEG       ((uint8_t)SERVO_ANGLE_MAX_DEG_CFG)

/** Dải góc chuẩn SG90 khi học menu (0..180 → ~1..2 ms). */
#define SERVO_LEARN_RAW_CLOSE_DEG ((uint8_t)0U)
#define SERVO_LEARN_RAW_OPEN_DEG  ((uint8_t)180U)

/** Thời gian ramp đều 0% → 100%. */
#define SERVO_RAMP_FULL_MS        (5000U)

/** Xung mặc định: TIM1 50 Hz, chu kỳ 20000 (~1 ms / ~2 ms). */
#define SERVO_PULSE_MIN_TICKS     ((uint16_t)1000U)
#define SERVO_PULSE_MAX_TICKS     ((uint16_t)2000U)
#define SERVO_PERIOD_TICKS        ((uint16_t)20000U)

/* =========================================================================
 * Kiểu dữ liệu
 * ========================================================================= */

/** Một kênh PWM (TIM + channel). */
typedef struct
{
	TIM_HandleTypeDef *htim;
	uint32_t channel;
} servo_pwm_ch_t;

/**
 * Cấu hình phần cứng — truyền vào @ref servo_init (giống lcd_i2c_config_t).
 *
 * - count: số servo thực tế (<= SERVO_MAX_COUNT)
 * - ch[]:  htim + channel cho từng servo
 * - pulse_min/max: 0 = dùng SERVO_PULSE_*_TICKS
 */
typedef struct
{
	uint8_t count;
	servo_pwm_ch_t ch[SERVO_MAX_COUNT];
	uint16_t pulse_min;
	uint16_t pulse_max;
} servo_config_t;

/** Góc đóng/mở đã học (lưu Flash, theo chỉ số). */
typedef struct
{
	uint8_t close_deg[SERVO_MAX_COUNT];
	uint8_t open_deg[SERVO_MAX_COUNT];
} servo_cal_t;

/** Handle runtime sau @ref servo_init. */
typedef struct
{
	uint8_t count;
	servo_pwm_ch_t pwm[SERVO_MAX_COUNT];
	uint8_t close_deg[SERVO_MAX_COUNT];
	uint8_t open_deg[SERVO_MAX_COUNT];
	uint8_t angle_deg[SERVO_MAX_COUNT];
	int16_t permille[SERVO_MAX_COUNT];
	int16_t target_permille[SERVO_MAX_COUNT];
	uint16_t pulse_min;
	uint16_t pulse_max;
	uint32_t ramp_last_tick;
	bool initialized;
} servo_t;

/* =========================================================================
 * Macro cấu hình board mặc định (2 servo TIM1 CH3/CH4)
 * ========================================================================= */

/** Khởi tạo struct config cho board hiện tại (2 SG90 trên TIM1). */
#define SERVO_BOARD_DEFAULT_CONFIG(HTIM_PTR) \
	{ \
		.count = SERVO_COUNT, \
		.ch = { \
			{ .htim = (HTIM_PTR), .channel = TIM_CHANNEL_3 }, \
			{ .htim = (HTIM_PTR), .channel = TIM_CHANNEL_4 }, \
		}, \
		.pulse_min = 0U, \
		.pulse_max = 0U, \
	}

/* =========================================================================
 * API
 * ========================================================================= */

/** Gán giá trị mặc định phần mềm (chưa bind TIM, chưa start PWM). */
void servo_defaults(servo_t *h);

/**
 * @brief Khởi tạo servo: copy config, start PWM, đặt cal mặc định.
 * @return false nếu cfg NULL, count=0, hoặc thiếu htim.
 */
bool servo_init(servo_t *h, const servo_config_t *cfg);

/** Đặt target tất cả servo về góc đóng. */
void servo_all_off(servo_t *h);

/** Áp permille hiện tại ra PWM (sau init / all_off). */
void servo_sync_present(servo_t *h);

void servo_apply_cal(servo_t *h, const servo_cal_t *cal);

uint8_t servo_close_deg(const servo_t *h, uint8_t idx);
uint8_t servo_open_deg(const servo_t *h, uint8_t idx);
/** idx: 0..count-1; percent 0%=đóng, 100%=mở. */
uint8_t servo_angle_from_percent(const servo_t *h, uint8_t idx, uint8_t percent);

/** Đặt góc đích (có ramp). idx: 0..count-1. */
void servo_set_angle(servo_t *h, uint8_t idx, uint8_t angle_deg);
/** Áp góc ngay (bỏ ramp). */
void servo_set_angle_live(servo_t *h, uint8_t idx, uint8_t angle_deg);
/** Học góc: 0..180 map thẳng xung SG90. */
void servo_set_learn_preview(servo_t *h, uint8_t idx, uint8_t angle_deg);
void servo_set_pulse_ticks(servo_t *h, uint8_t idx, uint16_t pulse_ticks);

uint8_t servo_get_angle(const servo_t *h, uint8_t idx);

/** Đồng bộ permille từ PWM hiện tại (thoát menu học). */
void servo_resync_all(servo_t *h);

/** Gọi định kỳ ~20 ms từ task output. */
void servo_ramp_update(servo_t *h, uint32_t now_tick);

#ifdef __cplusplus
}
#endif

#endif /* SERVO_H_ */
