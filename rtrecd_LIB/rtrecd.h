/*
 * rtrecd.h
 *
 *  Created on: 16 thg 3, 2026
 *      Author: embedded
 */

#ifndef RTRECD_H_
#define RTRECD_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "stm32f1xx_hal.h"
#include "cmsis_os.h"

/**
 * @file rtrecd.h
 * @brief Thư viện xử lý rotary encoder cơ học có nút nhấn.
 *
 * Cách dùng điển hình:
 * - Khai báo và cấu hình một biến @ref rtrecd_t.
 * - Gọi @ref rtrecd_init trong lúc khởi tạo hệ thống.
 * - Gọi @ref rtrecd_isr_a, @ref rtrecd_isr_b hoặc @ref rtrecd_isr_ab trong callback EXTI.
 * - Gọi @ref rtrecd_process theo chu kỳ trong task để lấy @ref rtrecd_queue_item_t.
 *
 * Thư viện:
 * - Giải mã quadrature A/B bằng ngắt GPIO.
 * - Debounce nút nhấn SW bằng tick mili giây, không dùng HAL_Delay().
 * - Phát sinh event quay CW/CCW, nhấn ngắn và nhấn dài.
 * - Có thể gọi @ref rtrecd_init trong task sau khi scheduler đã chạy.
 */

/**
 * @brief Giá trị debounce mặc định cho nút nhấn, đơn vị mili giây.
 */
#define RTRECD_DEFAULT_DEBOUNCE_MS      ((uint16_t)50U)

/**
 * @brief Giá trị ngưỡng nhấn dài mặc định, đơn vị mili giây.
 */
#define RTRECD_DEFAULT_LONG_PRESS_MS    ((uint16_t)1000U)
#define RTRECD_DEFAULT_STEPS_PER_DETENT ((uint8_t)4U)

/** Sau nhấn/nhả nút: bỏ qua xung quay (ms) — tránh nhiễu cơ khi ấn SW. */
#define RTRECD_ROT_SUPPRESS_MS          ((uint16_t)50U)

/** Giới hạn tối đa số bước quay chờ trong ISR (tránh tràn uint8_t). */
#define RTRECD_ROT_PENDING_MAX          ((uint8_t)32U)

/**
 * @brief Mô tả một chân GPIO theo dạng port/pin.
 */
typedef struct
{
	GPIO_TypeDef *port; /**< Cổng GPIO, ví dụ GPIOA/GPIOB. */
	uint16_t pin;       /**< Mã chân GPIO, ví dụ GPIO_PIN_12. */
} rtrecd_gpio_t;

/**
 * @brief Kiểu hàm lấy thời gian hệ thống theo mili giây.
 *
 * Thư viện dùng callback này để debounce và phát hiện nhấn dài.
 */
typedef uint32_t (*rtrecd_get_tick_ms_fn_t)(void);

/**
 * @brief Các event mức cao mà task nhận được từ encoder.
 */
typedef enum
{
	RTRECD_EVENT_NONE = 0,     /**< Không có thay đổi. */
	RTRECD_EVENT_ROTATE_CW,    /**< Xoay theo chiều kim đồng hồ. */
	RTRECD_EVENT_ROTATE_CCW,   /**< Xoay ngược chiều kim đồng hồ. */
	RTRECD_EVENT_BUTTON_SHORT, /**< Nút được nhấn ngắn. */
	RTRECD_EVENT_BUTTON_LONG   /**< Nút được nhấn dài. */
} rtrecd_queue_item_t;

/**
 * @brief Handle quản lý một encoder.
 *
 * Ứng dụng chỉ cần cấu hình các chân `pin_a`, `pin_b`, `pin_sw`.
 * Các trường cấu hình còn lại có thể để mặc định 0/NULL để thư viện tự bổ sung.
 *
 * Giá trị mặc định:
 * - `get_tick_ms = HAL_GetTick`
 * - `debounce_ms = RTRECD_DEFAULT_DEBOUNCE_MS`
 * - `long_press_ms = RTRECD_DEFAULT_LONG_PRESS_MS`
 * - `steps_per_detent = RTRECD_DEFAULT_STEPS_PER_DETENT`
 * - active level mặc định của A/B và SW là active low
 *
 * @warning Không nên sửa các trường trạng thái nội bộ sau khi đã init.
 */
typedef struct
{
	/* Cấu hình do người dùng gán */
	rtrecd_gpio_t pin_a;               /**< Chân tín hiệu A của encoder. */
	rtrecd_gpio_t pin_b;               /**< Chân tín hiệu B của encoder. */
	rtrecd_gpio_t pin_sw;              /**< Chân nút nhấn SW của encoder. */
	rtrecd_get_tick_ms_fn_t get_tick_ms; /**< Hàm lấy tick mili giây. NULL => dùng HAL_GetTick. */
	uint16_t debounce_ms;              /**< Thời gian debounce nút nhấn, ms. 0 => dùng mặc định. */
	uint16_t long_press_ms;            /**< Ngưỡng nhấn dài, ms. 0 => dùng mặc định. */
	bool ab_active_low;                /**< true nếu A/B tác động mức thấp. */
	bool sw_active_low;                /**< true nếu SW tác động mức thấp. */
	bool use_default_active_level;     /**< true để ép A/B và SW dùng active low mặc định. */
	uint8_t steps_per_detent;          /**< Số transition A/B để tính 1 nấc cơ học. 0 => dùng mặc định. */

	/* Trạng thái nội bộ của thư viện */
	volatile int8_t step_acc;          /**< Bộ tích lũy transition quadrature trước khi quy đổi ra 1 event quay. */
	volatile uint8_t prev_ab_state;    /**< Trạng thái A/B trước đó để giải mã quadrature. */
	volatile uint8_t rot_cw_pending;   /**< Số event quay CW đang chờ task lấy ra. */
	volatile uint8_t rot_ccw_pending;  /**< Số event quay CCW đang chờ task lấy ra. */
	bool btn_raw_state;                /**< Trạng thái thô hiện tại của nút nhấn. */
	bool btn_stable_state;             /**< Trạng thái ổn định sau debounce. */
	bool btn_pressing;                 /**< Cờ cho biết nút đang được nhấn ổn định. */
	bool btn_long_reported;            /**< Cờ cho biết event nhấn dài đã được phát. */
	uint32_t btn_last_bounce_ms;       /**< Mốc thời gian thay đổi trạng thái thô gần nhất. */
	uint32_t btn_press_start_ms;       /**< Mốc thời gian bắt đầu nhấn ổn định. */
	uint32_t rot_suppress_until_ms;    /**< Đến tick này: không phát event quay (sau nhấn SW). */
	bool initialized;                  /**< Cờ nội bộ: true sau khi handle đã được init. */
} rtrecd_t;

/**
 * @brief Khởi tạo một handle encoder.
 *
 * Hàm bổ sung giá trị mặc định còn thiếu và đồng bộ trạng thái ban đầu.
 *
 * @param h Con trỏ tới handle encoder cần khởi tạo.
 * @return true nếu khởi tạo thành công, false nếu tham số không hợp lệ.
 */
bool rtrecd_init(rtrecd_t *h);

/**
 * @brief Callback ngắt cho kênh A của encoder.
 *
 * Gọi hàm này trong ISR hoặc callback EXTI ứng với chân A.
 */
void rtrecd_isr_a(rtrecd_t *h);

/**
 * @brief Callback ngắt cho kênh B của encoder.
 *
 * Gọi hàm này trong ISR hoặc callback EXTI ứng với chân B.
 */
void rtrecd_isr_b(rtrecd_t *h);

/**
 * @brief Callback ngắt dùng chung cho hai kênh A/B.
 */
void rtrecd_isr_ab(rtrecd_t *h);

/**
 * @brief Callback ngắt cho chân SW của encoder.
 *
 * Gọi hàm này trong ISR hoặc callback EXTI ứng với chân SW (nút nhấn).
 * Việc bắt cạnh SW bằng ngắt giúp không bỏ sót nhấn ngắn ngay cả khi
 * task polling bị trễ.
 */
void rtrecd_isr_sw(rtrecd_t *h);

/**
 * @brief Xử lý định kỳ và trả về event của encoder.
 *
 * Hàm này cần được gọi theo chu kỳ trong task input. Mỗi lần gọi sẽ:
 * - Lấy tối đa 1 event quay đang pending do ISR tạo ra.
 * - Debounce nút nhấn SW.
 * - Phát hiện nhấn ngắn hoặc nhấn dài.
 *
 * @param h Con trỏ tới handle encoder.
 * @return Event phát sinh. Nếu không có thay đổi thì trả về RTRECD_EVENT_NONE.
 *
 * @note Hàm này không chặn và không dùng HAL_Delay().
 */
rtrecd_queue_item_t rtrecd_process(rtrecd_t *h);

/**
 * @brief Lấy trạng thái ổn định hiện tại của nút nhấn.
 *
 * @param h Con trỏ tới handle encoder.
 * @return true nếu nút đang được nhấn ổn định, false nếu đang nhả.
 */
bool rtrecd_is_button_pressed(const rtrecd_t *h);

/**
 * @brief Chuyển event sang chuỗi để debug/log.
 *
 * @param event Mã event cần chuyển.
 * @return Chuỗi hằng tương ứng với event.
 */
const char *rtrecd_queue_item_to_str(rtrecd_queue_item_t event);

/**
 * @brief Hàm service định kỳ cho encoder: xử lý và đẩy event vào queue.
 *
 * Gọi hàm này theo chu kỳ trong task input. Lặp gọi @ref rtrecd_process
 * và osMessageQueuePut cho đến khi hết event hoặc queue đầy.
 *
 * @param h     Con trỏ tới handle encoder.
 * @param queue Handle của osMessageQueue nhận rtrecd_queue_item_t.
 * @return true nếu có ít nhất một event được đẩy vào queue, false nếu không có gì.
 */
bool rtrecd_service(rtrecd_t *h, osMessageQueueId_t queue);

#ifdef __cplusplus
}
#endif

#endif /* RTRECD_H_ */
