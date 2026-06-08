/*
 * app_settings.h
 *
 * Lưu/Load cài đặt (chế độ vận hành + MinMax) vào Flash.
 *
 * Flash layout STM32F103C8 (64KB, page size = 1KB):
 *   Page 63 = 0x0800FC00 : DS18B20 position map  (thư viện ds18b20_uart)
 *   Page 62 = 0x0800F800 : App settings           (module này)
 *
 * Nội dung lưu:
 *   - active_mode       (chế độ đang chạy)
 *   - mode_cfg[4]       (MinMax cho 4 chế độ: Chay to / Dinh ghim / Qua the / Thanh trung)
 *
 * Chiến lược bảo vệ Flash:
 *   - Chỉ ghi khi user nhấn DÀI để thoát ra MAIN_MENU hoặc màn hình làm việc.
 *   - Cờ settings_dirty trong ctx chỉ set khi thực sự có thay đổi.
 *   - Xác thực bằng magic number + checksum khi load.
 */

#ifndef APP_SETTINGS_H_
#define APP_SETTINGS_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "app_menu.h"

/* =========================================================================
 * Flash address — KHÔNG được trùng với ONEWIRE_POSMAP_FLASH_ADDR (0x0800FC00)
 * ========================================================================= */

/** Page 62 của STM32F103C8 (64KB Flash, page size 1KB) */
#define APP_SETTINGS_FLASH_ADDR   0x0800F800UL

#define APP_SETTINGS_MAGIC        0xCA5E1A2BUL  /**< Giá trị nhận dạng hợp lệ */
#define APP_SETTINGS_VERSION      11U           /**< v11: servo_cal; v10: fan_learned_rpm */

/* =========================================================================
 * API
 * ========================================================================= */

/**
 * @brief Load cài đặt từ Flash vào ctx.
 * @note  Gọi từ app_menu_init(). Nếu Flash chưa có dữ liệu hợp lệ → giữ
 *        nguyên giá trị mặc định đã được set trong app_menu_init().
 */
void app_settings_load(app_menu_ctx_t *ctx);

/**
 * @brief Ghi cài đặt hiện tại từ ctx vào Flash.
 * @note  Xóa page trước khi ghi (erase + write). Thực hiện trong vài ms.
 *        Nên gọi từ task khi không có thao tác cảm biến quan trọng.
 *        Tự clear cờ ctx->settings_dirty sau khi ghi thành công.
 */
void app_settings_save(app_menu_ctx_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* APP_SETTINGS_H_ */
