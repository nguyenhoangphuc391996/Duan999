/*
 * app_menu.h
 *
 * Menu điều khiển môi trường nhà trồng nấm.
 * Điều hướng bằng EC11 Rotary Encoder:
 *   - Xoay CW/CCW : di chuyển con trỏ hoặc thay đổi giá trị
 *   - Nhấn ngắn   : vào / xác nhận
 *   - Nhấn dài    : quay lại
 *
 * Cấu trúc menu:
 *  [WORK1] <CW> [WORK2]  (SHORT để vào MAIN_MENU)
 *  MAIN_MENU
 *    ├─ Chon che do     -> SCREEN_MODE_SELECT
 *    ├─ Cai dat t/gian  -> SCREEN_TIME_MENU -> SCREEN_TIME_EDIT
 *    ├─ Cai dat MinMax  -> SCREEN_MINMAX_MODE -> PARAM -> FIELD -> EDIT
 *    ├─ Vi tri DS18B20  -> SCREEN_DS18B20_POS
 *    └─ Hoc toc do quat -> SCREEN_FAN_LEARN_MENU -> SCREEN_FAN_LEARN_RUN
 */

#ifndef APP_MENU_H_
#define APP_MENU_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "rtrecd.h"
#include "scd4x_i2c.h"
#include "ds18b20_app.h"

/* =========================================================================
 * Constants
 * ========================================================================= */

#define MENU_NAV_DEPTH    10   /**< Độ sâu tối đa của navigation stack */
#define MENU_DS18B20_MAX   6   /**< Số cảm biến DS18B20 tối đa (6 vị trí) */

/** Tốc độ quạt mặc định (%) cho chế độ Chay to / Kich Dinh ghim / Dinh ghim / Qua the. */
#define MODE_DEFAULT_FAN_PERCENT  100U

/* =========================================================================
 * Enums
 * ========================================================================= */

/** @brief Tất cả màn hình trong hệ thống menu */
typedef enum
{
    SCREEN_WORK1 = 0,      /**< Màn hình làm việc 1 (sensor realtime)        */
    SCREEN_WORK2,          /**< Màn hình làm việc 2 (DS18B20 từng vị trí)   */
    SCREEN_WORK3,          /**< Màn hình làm việc 3 (MinMax chế độ hiện tại)*/
    SCREEN_MAIN_MENU,      /**< Menu chính                                   */
    SCREEN_MODE_SELECT,    /**< Chọn chế độ vận hành                        */
    SCREEN_TIME_MENU,      /**< Danh sách trường thời gian                   */
    SCREEN_TIME_EDIT,      /**< Chỉnh sửa 1 trường thời gian                */
    SCREEN_MINMAX_MODE,    /**< Chọn chế độ để cài MinMax                   */
    SCREEN_MINMAX_PARAM,   /**< Chọn thông số (Nhiệt độ/Độ ẩm/CO2/Đèn/Tốc độ quạt) */
    SCREEN_MINMAX_FIELD,   /**< Chọn trường (Min/Max hoặc Start/Stop)       */
    SCREEN_MINMAX_EDIT,    /**< Chỉnh sửa giá trị MinMax                    */
    SCREEN_DS18B20_POS,    /**< Sub-menu vị trí DS18B20                     */
    SCREEN_DS18B20_COUNT,  /**< Chọn số cảm biến DS18B20                    */
    SCREEN_DS18B20_LEARN,  /**< Học vị trí DS18B20 (hiển thị tiến trình)    */
    SCREEN_FAN_LEARN_MENU, /**< Sub-menu học tốc độ quạt (Hoc / Thoat)      */
    SCREEN_FAN_LEARN_RUN,  /**< Đang học tốc độ quạt (hiển thị tiến trình) */
    SCREEN_COUNT
} app_screen_t;

/** @brief Chế độ vận hành */
typedef enum
{
    MODE_CHAY_TO = 0,
    MODE_KICH_DINH_GHIM,   /**< Kích định ghim (đèn bật 24/24) */
    MODE_DINH_GHIM,
    MODE_QUA_THE,
    MODE_THANH_TRUNG,
    MODE_NGHI,
    MODE_COUNT
} app_mode_t;

/** @brief Chế độ hoạt động của đèn */
typedef enum
{
    DEN_MODE_SU_DUNG      = 0,  /**< Dùng Time Start / Time Stop */
    DEN_MODE_KHONG_SU_DUNG = 1, /**< Không dùng đèn (luôn tắt)  */
    DEN_MODE_24_24        = 2,  /**< Bật đèn 24/24 (luôn bật)   */
} den_mode_t;

/** @brief Thông số MinMax */
typedef enum
{
    PARAM_NHIET_DO = 0,
    PARAM_DO_AM,
    PARAM_CO2,
    PARAM_DEN,
    PARAM_TOC_DO_QUAT,
    PARAM_COUNT
} minmax_param_t;

/* =========================================================================
 * DS18B20 learn phase constants
 * ========================================================================= */

#define DS18B20_LEARN_IDLE       0U  /**< Không trong quá trình học          */
#define DS18B20_LEARN_SEARCHING  1U  /**< Đang tìm / học vị trí              */
#define DS18B20_LEARN_DONE       2U  /**< Học xong, đã lưu Flash             */
#define DS18B20_LEARN_ERROR      3U  /**< Hết thời gian, không tìm thấy đủ CB*/

/* =========================================================================
 * Fan learn constants
 * ========================================================================= */

/** Số bước học: 0%, 10%, 20%, ..., 100% (11 bước). */
#define FAN_LEARN_STEPS          11U

#define FAN_LEARN_IDLE           0U  /**< Chưa học / không trong quá trình học */
#define FAN_LEARN_RUNNING        1U  /**< Đang học                             */
#define FAN_LEARN_DONE           2U  /**< Học xong, có dữ liệu hợp lệ         */
#define FAN_LEARN_ERROR          3U  /**< Lỗi                                  */

/* =========================================================================
 * Settings structures
 * ========================================================================= */

/** @brief Cặp ngưỡng Min/Max (đơn vị: x10 cho nhiệt/ẩm, ppm cho CO2) */
typedef struct
{
    int16_t min;
    int16_t max;
} minmax_range_t;

/** @brief Cài đặt thời gian bật/tắt đèn */
typedef struct
{
    uint8_t time_start_h;  /**< Giờ bật   */
    uint8_t time_start_m;  /**< Phút bật  */
    uint8_t time_stop_h;   /**< Giờ tắt   */
    uint8_t time_stop_m;   /**< Phút tắt  */
    uint8_t den_mode;      /**< den_mode_t: 0=Su dung, 1=Khong su dung, 2=24/24 */
} minmax_den_t;

/** @brief Cài đặt cho 1 chế độ (nhiet do / do am / co2 / den) */
typedef struct
{
    minmax_range_t nhiet_do;   /**< °C   : 20~35 (chay to/dinh ghim/qua the), 20~100 (thanh trung) */
    minmax_range_t do_am;      /**< %RH  : 50~95  */
    uint8_t        do_am_disabled; /**< 1 = không dùng máy tạo ẩm (do_am bị tắt) */
    uint8_t        _pad1;          /**< padding để căn chỉnh 2-byte */
    minmax_range_t co2;        /**< ppm  : 400~5000 (bước 100) */
    minmax_den_t   den;        /**< giờ  : 0~23 (time_start_h / time_stop_h) */
    uint8_t        toc_do_quat; /**< Tốc độ quạt 0~100% (chế độ Chay to / Kich Dinh ghim / Dinh ghim / Qua the) */
    uint8_t        _pad2;       /**< padding căn chỉnh */

    /* --- Chỉ dùng cho chế độ Thanh trùng --- */
    uint16_t thanh_trung_initial_minutes; /**< số phút ban đầu mở SG90 ở mức to */
    uint8_t  sg90_mo_to_deg;              /**< góc mở mức to (0..180) */
    uint8_t  sg90_mo_nho_deg;             /**< góc mở mức nhỏ (0..180) */
} mode_settings_t;

/** @brief Cài đặt thời gian thực */
typedef struct
{
    uint16_t year;
    uint8_t  month;
    uint8_t  day;
    uint8_t  hour;
    uint8_t  minute;
    uint8_t  second;
} app_time_t;

/* =========================================================================
 * Navigation stack frame
 * ========================================================================= */

/** @brief 1 frame trên navigation stack */
typedef struct
{
    app_screen_t screen;
    uint8_t      cursor;
    uint8_t      scroll;
} nav_frame_t;

/* =========================================================================
 * Main menu context
 * ========================================================================= */

typedef struct
{
    /* --- Navigation --- */
    nav_frame_t  stack[MENU_NAV_DEPTH]; /**< Navigation stack                */
    uint8_t      stack_top;             /**< Index của frame tiếp theo        */
    app_screen_t screen;                /**< Màn hình hiện tại               */
    uint8_t      cursor;                /**< Con trỏ trong list hiện tại     */
    uint8_t      scroll;                /**< Vị trí cuộn (top visible index) */
    bool         dirty;                 /**< true = cần vẽ lại LCD           */
    bool         time_rtc_dirty;        /**< true = time_cfg đã sửa, cần ghi RTC */
    bool         settings_dirty;        /**< true = mode/MinMax đã sửa, cần lưu Flash */

    /* --- Sensor data (cập nhật từ TaskUI) --- */
    scd41_queue_item_t scd41;
    bool               scd41_fault;  /**< true = SCD41 đang có lỗi (fault_active) */
    Ds18b20QueueItem   ds18b20[MENU_DS18B20_MAX];
    uint8_t            ds18b20_count;
    uint8_t            ds18b20_fault_mask; /**< bit i = cảm biến i+1 bị lỗi dây */

    /* --- Settings --- */
    app_mode_t      active_mode;
    mode_settings_t mode_cfg[5]; /**< [0]=Chay to [1]=Kich Dinh ghim [2]=Dinh ghim [3]=Qua the [4]=Thanh trung */
    app_time_t      time_cfg;
    uint8_t         ds18b20_role[MENU_DS18B20_MAX]; /**< Gán vai trò cho từng cảm biến */

    /* --- Edit context (dùng cho SCREEN_TIME_EDIT và SCREEN_MINMAX_EDIT) --- */
    int32_t edit_value;        /**< Giá trị đang chỉnh                       */
    int32_t edit_min;          /**< Giới hạn dưới                            */
    int32_t edit_max;          /**< Giới hạn trên                            */
    uint8_t edit_field_index;  /**< Index trường đang sửa (time field / min-max field) */
    uint8_t edit_mode_index;   /**< Index chế độ đang cài MinMax             */
    uint8_t edit_param_index;  /**< Index thông số đang cài MinMax           */
    uint8_t den_field_level;   /**< 0=top (Su dung/Khong/24/24), 1=su dung sub (Time Start/Stop) */
    uint8_t do_am_field_level; /**< 0=top (Su dung/Khong su dung), 1=min/max editing */

    /* --- DS18B20 position learning (shared với TaskDS18B20, truy cập atomic) --- */
    uint8_t          ds18b20_target_count;   /**< Số CB mong muốn (1-MENU_DS18B20_MAX) */
    volatile uint8_t relearn_req;            /**< 1 = TaskDS18B20 cần thực hiện học lại */
    volatile uint8_t relearn_phase;          /**< DS18B20_LEARN_* constants             */
    volatile uint8_t relearn_retry_count;    /**< Số lần thử (timeout detection)        */
    volatile uint8_t relearn_current_pos;    /**< Vị trí đang học (1-based); 0=warmup   */
    volatile uint8_t relearn_pos_found;      /**< 0=đang hỏi người dùng, 1=đã tìm thấy */

    /* --- Học tốc độ quạt (shared với TaskFanLearn) --- */
    volatile uint8_t  fan_learn_req;          /**< 1 = bắt đầu học ngay                   */
    volatile uint8_t  fan_learn_phase;        /**< FAN_LEARN_* constants                   */
    volatile uint8_t  fan_learn_step;         /**< Bước hiện tại (0..FAN_LEARN_STEPS-1)   */
    volatile uint8_t  fan_learn_active;       /**< 1 = đang học, override fan control      */
    volatile uint8_t  fan_learn_pwm_pct;      /**< PWM % mà learning task yêu cầu đặt     */
    uint8_t           fan_learn_done;         /**< 1 = có dữ liệu học hợp lệ              */
    uint16_t          fan_learned_tach[FAN_LEARN_STEPS]; /**< xung TACH/1 s tại 0%,10%,...,100% */

    /* --- Cảnh báo tốc độ quạt --- */
    uint8_t           fan_alarm_active;       /**< 1 = đang phát cảnh báo quạt (PB5)      */

} app_menu_ctx_t;

/* =========================================================================
 * Public API
 * ========================================================================= */

/**
 * @brief Khởi tạo context menu, gán giá trị mặc định.
 * @note  Gọi từ TaskLCD trước vòng lặp chính.
 */
void app_menu_init(app_menu_ctx_t *ctx);

/**
 * @brief Xử lý 1 event từ EC11 encoder.
 * @note  Gọi từ TaskUI khi có event mới trong QueueEC11.
 */
void app_menu_handle_event(app_menu_ctx_t *ctx, rtrecd_queue_item_t ev);

/**
 * @brief Vẽ lại LCD nếu có thay đổi (dirty flag).
 * @note  Gọi từ TaskLCD theo chu kỳ.
 */
void app_menu_render(app_menu_ctx_t *ctx);

/**
 * @brief Cập nhật dữ liệu SCD41 vào context.
 * @note  Gọi từ TaskUI khi nhận được data từ QueueSCD41.
 */
void app_menu_update_scd41(app_menu_ctx_t *ctx, const scd41_queue_item_t *data);

/**
 * @brief Cập nhật dữ liệu DS18B20 vào context.
 * @note  Gọi từ TaskUI khi nhận được data từ QueueDS18B20.
 */
void app_menu_update_ds18b20(app_menu_ctx_t *ctx, const Ds18b20QueueItem *data);

/**
 * @brief Đánh dấu dirty để TaskLCD vẽ lại màn hình làm việc.
 * @note  Gọi sau khi cập nhật sensor data.
 */
void app_menu_mark_dirty(app_menu_ctx_t *ctx);

/**
 * @brief Cập nhật trạng thái lỗi SCD41 vào context.
 * @note  Gọi từ TaskUI khi nhận được fault event từ SCD41.
 */
void app_menu_set_scd41_fault(app_menu_ctx_t *ctx, bool fault);

/**
 * @brief Đọc thời gian từ RTC phần cứng và cập nhật vào ctx->time_cfg.
 * @note  Gọi định kỳ từ TaskUI (mỗi 1 giây) để màn hình làm việc luôn cập nhật.
 * @param ctx   Con trỏ menu context.
 * @param hrtc  Con trỏ RTC handle (từ CubeMX).
 */
void app_menu_update_time_from_rtc(app_menu_ctx_t *ctx, RTC_HandleTypeDef *hrtc);

/**
 * @brief Ghi ctx->time_cfg vào RTC phần cứng.
 * @note  Gọi từ TaskUI sau khi phát hiện ctx->time_rtc_dirty == true.
 *        Hàm tự clear cờ time_rtc_dirty sau khi ghi xong.
 * @param ctx   Con trỏ menu context.
 * @param hrtc  Con trỏ RTC handle (từ CubeMX).
 */
void app_menu_write_time_to_rtc(app_menu_ctx_t *ctx, RTC_HandleTypeDef *hrtc);

#ifdef __cplusplus
}
#endif

#endif /* APP_MENU_H_ */
