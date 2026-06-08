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
 *    ├─ Hoc toc do quat -> SCREEN_FAN_LEARN_MENU -> SCREEN_FAN_LEARN_RUN
 *    └─ Hoc goc servo  -> SERVO_LEARN_MENU -> SERVO_LEARN_SIDE -> SERVO_LEARN_EDIT
 */

#ifndef APP_MENU_H_
#define APP_MENU_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "fan.h"           /* fan_state_t, FAN_LEARN_* constants, FAN_LEARN_STEPS */
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
    SCREEN_SERVO_LEARN_MENU, /**< Chọn Servo 1 / Servo 2                    */
    SCREEN_SERVO_LEARN_SIDE, /**< Chọn góc đóng / góc mở                    */
    SCREEN_SERVO_LEARN_EDIT, /**< Chỉnh góc + quay servo thử                */
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

#define DS18B20_LEARN_IDLE       0U
#define DS18B20_LEARN_SEARCHING  1U
#define DS18B20_LEARN_DONE       2U
#define DS18B20_LEARN_ERROR      3U

/* =========================================================================
 * Settings structures
 * ========================================================================= */

/** @brief Cặp ngưỡng Min/Max */
typedef struct
{
    int16_t min;
    int16_t max;
} minmax_range_t;

/** @brief Cài đặt thời gian bật/tắt đèn */
typedef struct
{
    uint8_t time_start_h;
    uint8_t time_start_m;
    uint8_t time_stop_h;
    uint8_t time_stop_m;
    uint8_t den_mode;
} minmax_den_t;

/** @brief Cài đặt cho 1 chế độ */
typedef struct
{
    minmax_range_t nhiet_do;
    minmax_range_t do_am;
    uint8_t        do_am_disabled;
    uint8_t        _pad1;
    minmax_range_t co2;
    minmax_den_t   den;
    uint8_t        toc_do_quat;
    uint8_t        _pad2;
    uint16_t thanh_trung_initial_minutes;
    uint8_t  sg90_mo_to_pct;   /**< % mở to (0=đóng/MIN, 100=mở/MAX) */
    uint8_t  sg90_mo_nho_pct;  /**< % mở nhỏ */
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

typedef struct
{
    app_screen_t screen;
    uint8_t      cursor;
    uint8_t      scroll;
} nav_frame_t;

/* =========================================================================
 * Main menu context
 *
 * Chú ý: struct có tag "app_menu_ctx_s" để fan.h có thể forward-declare.
 *         fan.h được include phía trên trước khi struct này được định nghĩa,
 *         nên forward declaration trong fan.h được hoàn chỉnh tại đây.
 * ========================================================================= */

/*
 * Struct được định nghĩa với tag "app_menu_ctx_s" khớp với forward declaration
 * trong fan.h. Typedef "app_menu_ctx_t" đã khai báo trong fan.h nên không khai
 * báo lại ở đây để tương thích C99.
 */
struct app_menu_ctx_s
{
    /* --- Navigation --- */
    nav_frame_t  stack[MENU_NAV_DEPTH];
    uint8_t      stack_top;
    app_screen_t screen;
    uint8_t      cursor;
    uint8_t      scroll;
    bool         dirty;
    bool         time_rtc_dirty;
    bool         settings_dirty;

    /* --- Sensor data --- */
    scd41_queue_item_t scd41;
    bool               scd41_fault;
    Ds18b20QueueItem   ds18b20[MENU_DS18B20_MAX];
    uint8_t            ds18b20_count;
    uint8_t            ds18b20_fault_mask;

    /* --- Settings --- */
    app_mode_t      active_mode;
    mode_settings_t mode_cfg[5];
    app_time_t      time_cfg;
    uint8_t         ds18b20_role[MENU_DS18B20_MAX];

    /* --- Edit context --- */
    int32_t edit_value;
    int32_t edit_min;
    int32_t edit_max;
    uint8_t edit_field_index;
    uint8_t edit_mode_index;
    uint8_t edit_param_index;
    uint8_t den_field_level;
    uint8_t do_am_field_level;

    /* --- DS18B20 position learning --- */
    uint8_t          ds18b20_target_count;
    volatile uint8_t relearn_req;
    volatile uint8_t relearn_phase;
    volatile uint8_t relearn_retry_count;
    volatile uint8_t relearn_current_pos;
    volatile uint8_t relearn_pos_found;

    /* --- Quạt: trạng thái chia sẻ với fan_lib --- */
    fan_state_t      fan;  /**< Học RPM + cảnh báo + an toàn; truy cập qua MutexMenuHandle */

    /* --- Học góc servo SG90 --- */
    output_servo_cal_t servo_cal;
    volatile uint8_t   servo_learn_active;
    uint8_t            servo_learn_servo;   /**< 0=servo1, 1=servo2 */
    uint8_t            servo_learn_side;    /**< 0=đóng, 1=mở */
    volatile uint8_t   servo_learn_angle_deg;
    uint8_t            servo_learn_saved_deg;

};

/* =========================================================================
 * Public API
 * ========================================================================= */

void app_menu_init(app_menu_ctx_t *ctx);
void app_menu_handle_event(app_menu_ctx_t *ctx, rtrecd_queue_item_t ev);
void app_menu_render(app_menu_ctx_t *ctx);
void app_menu_update_scd41(app_menu_ctx_t *ctx, const scd41_queue_item_t *data);
void app_menu_update_ds18b20(app_menu_ctx_t *ctx, const Ds18b20QueueItem *data);
void app_menu_mark_dirty(app_menu_ctx_t *ctx);
void app_menu_set_scd41_fault(app_menu_ctx_t *ctx, bool fault);
void app_menu_update_time_from_rtc(app_menu_ctx_t *ctx, RTC_HandleTypeDef *hrtc);
void app_menu_write_time_to_rtc(app_menu_ctx_t *ctx, RTC_HandleTypeDef *hrtc);

#ifdef __cplusplus
}
#endif

#endif /* APP_MENU_H_ */
