/*
 * fan.h
 *
 * Thư viện điều khiển quạt:
 *   - Học tốc độ RPM (bảng 0%→100%, bước 10%)
 *   - Giám sát RPM so với profile đã học (±25%, 2 lần xác nhận)
 *   - Giám sát an toàn cơ học (<100 RPM liên tục 2 s)
 *   - Phục hồi lỗi: chờ RPM về mức học ở 0% → quay tay → cho chạy lại
 *   - Debug qua ITM Console (ITM_LIB_FAN)
 *
 * Phụ thuộc:
 *   - cmsis_os.h   : osMutexId_t, osDelay, ...
 *   - output.h     : output_t, output_fan_set_percent
 *   - stm32f1xx_hal.h : GPIO (qua output.h)
 */

#ifndef FAN_LIB_FAN_H_
#define FAN_LIB_FAN_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "cmsis_os.h"
#include "output.h"

/* =========================================================================
 * Hằng số – Fan learn & safety
 * ========================================================================= */

/** Số bước học: 0%, 10%, 20%, ..., 100% (11 bước). */
#define FAN_LEARN_STEPS               11U

#define FAN_LEARN_IDLE                0U  /**< Chưa học / không trong quá trình học */
#define FAN_LEARN_WAIT_STOP           1U  /**< Tắt quạt, chờ hết quán tính          */
#define FAN_LEARN_RUNNING             2U  /**< Đang học                              */
#define FAN_LEARN_DONE                3U  /**< Học xong, có profile hợp lệ          */
#define FAN_LEARN_ERROR               4U  /**< Lỗi (dự phòng)                       */

/** Số xung TACH / vòng (quạt 12V PC thường = 2). */
#define FAN_TACH_PULSES_PER_REV       2U
/** Chuyển xung/giây → RPM. */
#define FAN_PULSES_TO_RPM(pps) \
    ((uint16_t)(((uint32_t)(pps) * 60U) / (uint32_t)FAN_TACH_PULSES_PER_REV))

/** Ngưỡng lỗi tốc độ thấp: dưới 100 RPM → coi là kẹt/chậm. */
#define FAN_FAULT_RPM_MIN             100U
/** Phục hồi cơ học: RPM >= ngưỡng này mới cho quạt chạy lại. */
#define FAN_RECOVERY_RPM_MIN          100U
/** Chỉ giám sát an toàn khi PWM >= 10% (dưới ngưỡng này không đo được RPM). */
#define FAN_MONITOR_MIN_PWM_PERCENT   10U
/** Chờ khởi động (ms) sau khi đổi PWM trước khi bắt đầu đếm lỗi. */
#define FAN_STARTUP_GRACE_MS          2000U
/** < 100 RPM liên tục N giây → kích hoạt lỗi cơ học. */
#define FAN_LOW_SPEED_FAULT_SEC       2U
/** Ngưỡng sai số so với RPM đã học (±%). */
#define FAN_LEARN_TOLERANCE_PERCENT   20U
/** Sàn sai số tuyệt đối (RPM) – tránh báo nhầm do lượng tử hoá TACH. */
#define FAN_LEARN_TOLERANCE_RPM_MIN   60U
/** Chu kỳ lặp giám sát profile khi duty/chế độ đã ổn định (ms). */
#define FAN_MONITOR_INTERVAL_MS       3000U
/** Số lần liên tiếp lệch ngưỡng trước khi kích hoạt lỗi. */
#define FAN_LEARN_FAULT_CONFIRM_COUNT 3U
/** Xung TACH tối đa/giây để coi quạt đã dừng hẳn (1 xung ≈ nhiễu). */
#define FAN_COAST_STOP_MAX_PPS        1U
/** Số giây liên tục "dừng" để xác nhận hết quán tính. */
#define FAN_COAST_STOP_CONFIRM_SEC    2U
/** Hết thời gian chờ dừng thì bắt đầu học dù chưa xác nhận (ms). */
#define FAN_COAST_STOP_TIMEOUT_MS     15000U

/** Chờ PWM ổn định (giây) – học: một lần trước các lần đo; giám sát: sau đổi duty/chế độ. */
#define FAN_LEARN_STEP_SETTLE_SEC     4U
/** Số giây đo RPM mỗi lần (trung bình 1 mẫu/giây) – dùng chung học và giám sát. */
#define FAN_LEARN_STEP_MEASURE_SEC    2U
/** Số lần đo liên tiếp khi học (sau settle) → trung bình các lần đo. */
#define FAN_LEARN_STEP_REPEAT_COUNT   2U
/** Chờ thêm (giây) sau khi xong một bước học rồi mới đổi sang % PWM tiếp theo. */
#define FAN_LEARN_STEP_GAP_SEC        1U

/* =========================================================================
 * Fan state – nhúng vào app_menu_ctx_t, truy cập qua mutex
 * ========================================================================= */

/**
 * @brief Trạng thái quạt chia sẻ giữa fan task và menu/display.
 *
 * Tất cả truy cập cần được bảo vệ bởi MutexMenuHandle, ngoại trừ
 * các trường volatile được fan task set và menu task chỉ đọc.
 */
typedef struct
{
    volatile uint8_t  learn_req;     /**< 1 = menu yêu cầu bắt đầu học       */
    volatile uint8_t  learn_phase;   /**< FAN_LEARN_* : trạng thái học        */
    volatile uint8_t  learn_step;    /**< Bước học hiện tại (0-10)            */
    volatile uint8_t  learn_active;  /**< 1 = đang học, override output_ctrl  */
    volatile uint8_t  learn_pwm_pct; /**< PWM % do fan task yêu cầu đặt      */
    uint8_t           learn_done;    /**< 1 = có profile RPM hợp lệ           */
    uint16_t          learned_rpm[FAN_LEARN_STEPS]; /**< RPM tại 0%,10%,...,100% */
    uint8_t           alarm_active;     /**< 1 = lệch >±25% RPM học (QER)    */
    uint8_t           low_speed_fault;  /**< 1 = <100 RPM liên tục 2 s       */
    uint8_t           force_off;        /**< 1 = cưỡng bức tắt quạt          */
} fan_state_t;

/* =========================================================================
 * Fan context – dùng trong fan.c và main.c, KHÔNG nhúng vào app_menu_ctx_t
 * ========================================================================= */

/**
 * @brief Forward declaration.
 *        Định nghĩa đầy đủ trong app_menu.h (struct app_menu_ctx_s).
 */
typedef struct app_menu_ctx_s app_menu_ctx_t;

/**
 * @brief Context đầy đủ của fan library.
 *        Khởi tạo 1 lần bằng fan_ctx_init() trước vòng lặp task.
 */
typedef struct
{
    app_menu_ctx_t   *menu_ctx;    /**< Con trỏ tới shared context (qua mutex)    */
    output_t         *output;      /**< Output handle (output_fan_set_percent)     */
    volatile uint32_t *tach_count; /**< Bộ đếm xung TACH tăng trong ISR           */
    osMutexId_t       mutex;       /**< MutexMenuHandle                            */

    /**
     * GPIO còi / báo động.
     * Đặt alarm_port = NULL nếu không dùng GPIO (fan_alarm_update() bỏ qua).
     */
    GPIO_TypeDef     *alarm_port;  /**< Ví dụ: GPIOB                              */
    uint16_t          alarm_pin;   /**< Ví dụ: GPIO_PIN_5                         */
} fan_ctx_t;

/* =========================================================================
 * Public API
 * ========================================================================= */

/**
 * @brief Khởi tạo fan context.
 * @note  Gọi một lần trước vòng lặp StartTaskFanLearn.
 *        Sau khi TIM3 IC đã được start (HAL_TIM_IC_Start_IT).
 */
void fan_ctx_init(fan_ctx_t        *fc,
                  app_menu_ctx_t   *menu_ctx,
                  output_t         *output,
                  volatile uint32_t *tach_count,
                  osMutexId_t       mutex,
                  GPIO_TypeDef     *alarm_port,
                  uint16_t          alarm_pin);

/**
 * @brief Thân vòng lặp fan task – gọi liên tục từ StartTaskFanLearn.
 *
 * Xử lý:
 *  - Học tốc độ quạt theo yêu cầu từ menu
 *  - Giám sát RPM so với profile đã học (5 s/lần)
 *  - Giám sát an toàn cơ học (1 s/lần)
 *
 * @note Hàm này gọi osDelay() trong quá trình học → chạy trong task context.
 */
void fan_task_body(fan_ctx_t *fc);

/**
 * @brief Cập nhật GPIO báo động theo mọi nguồn lỗi (quạt + cảm biến).
 *
 * @param fc            Fan context
 * @param scd41_fault   true nếu SCD41 đang có lỗi
 * @param ds18b20_mask  Bitmask DS18B20 lỗi (bit i = cảm biến i+1)
 */
void fan_alarm_update(fan_ctx_t *fc, bool scd41_fault, uint8_t ds18b20_mask);

/**
 * @brief true nếu có bất kỳ lỗi quạt nào.
 * @note  Caller phải đang giữ mutex.
 */
bool fan_has_any_fault_locked(const fan_state_t *state);

#ifdef __cplusplus
}
#endif

#endif /* FAN_LIB_FAN_H_ */
