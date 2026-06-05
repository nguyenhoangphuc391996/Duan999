/*
 * app_settings.c
 *
 * Lưu/Load cài đặt vào Flash page 62 (0x0800F800).
 *
 * Cấu trúc Flash (packed, tổng ~122 bytes, nằm gọn trong 1KB page):
 *
 *   [uint32_t magic      ]  4 bytes  — nhận dạng dữ liệu hợp lệ
 *   [uint8_t  version    ]  1 byte   — phiên bản cấu trúc
 *   [uint8_t  active_mode]  1 byte   — chế độ đang chọn
 *   [uint8_t  _pad[2]    ]  2 bytes  — căn chỉnh 4-byte
 *   [mode_settings_t ×5  ] ~110 bytes — MinMax 5 chế độ
 *   [uint32_t checksum   ]  4 bytes  — tổng kiểm tra
 *
 * Ghi Flash bằng HAL halfword (16-bit) để tương thích STM32F1.
 */

#include "app_settings.h"
#include "itm.h"
#include "stm32f1xx_hal.h"
#include <string.h>
#include <stddef.h>

/* =========================================================================
 * Internal Flash data structure
 * ========================================================================= */

typedef struct __attribute__((packed))
{
    uint32_t        magic;
    uint8_t         version;
    uint8_t         active_mode;
    uint8_t         _pad[2];          /* giữ mode_cfg căn chỉnh 4-byte */
    mode_settings_t mode_cfg[5];      /* 5 chế độ (Chay to/Kich Dinh ghim/Dinh ghim/Qua the/Thanh trung) */
    /* v9: dữ liệu học tốc độ quạt */
    uint16_t        fan_learned_tach[FAN_LEARN_STEPS]; /* xung TACH/1 s tại 0%,10%,...,100% */
    uint8_t         fan_learn_done;   /* 1 = dữ liệu hợp lệ */
    uint8_t         _pad_fan;         /* căn chỉnh chẵn cho halfword write */
    uint32_t        checksum;
} app_settings_flash_t;

/* Kiểm tra tại compile-time kích thước phải là bội của 2 (halfword write) */
_Static_assert((sizeof(app_settings_flash_t) % 2U) == 0U,
               "app_settings_flash_t size must be even for halfword Flash write");

/* =========================================================================
 * Checksum
 * ========================================================================= */

static uint32_t calc_checksum(const app_settings_flash_t *s)
{
    uint32_t sum = 0U;
    const uint8_t *p = (const uint8_t *)s;
    const size_t len = offsetof(app_settings_flash_t, checksum);
    for (size_t i = 0U; i < len; i++)
    {
        sum += (uint32_t)p[i];
    }
    return sum;
}

static void flash_log(const char *msg)
{
    itm_print(msg);
    itm_print("\r\n");
}

/* =========================================================================
 * Public API
 * ========================================================================= */

void app_settings_load(app_menu_ctx_t *ctx)
{
    const app_settings_flash_t *fs =
        (const app_settings_flash_t *)APP_SETTINGS_FLASH_ADDR;
    bool valid = true;

    flash_log("[FLASH] Load settings from Flash...");

    /* Kiểm tra magic */
    if (fs->magic != APP_SETTINGS_MAGIC)          valid = false;
    /* Kiểm tra version (v9 hiện tại; v7 và v8 được migrate) */
    if (valid && (fs->version != APP_SETTINGS_VERSION))
    {
        if (fs->version != 7U && fs->version != 8U)
        {
            valid = false;
        }
    }
    /* Kiểm tra checksum */
    if (valid && (fs->checksum != calc_checksum(fs)))        valid = false;
    /* Kiểm tra mode hợp lệ (không tính MODE_NGHI vì không có MinMax) */
    if (valid && (fs->active_mode >= (uint8_t)MODE_COUNT))   valid = false;

    if (!valid)
    {
        flash_log("[FLASH] Load settings INVALID -> use defaults");
        return;
    }

    /* Tất cả hợp lệ → nạp vào ctx */
    ctx->active_mode = (app_mode_t)fs->active_mode;
    memcpy(ctx->mode_cfg, fs->mode_cfg, sizeof(ctx->mode_cfg));

    /* v7 → v8/v9: toc_do_quat cũ mặc định 20% → chuẩn hoá 100% */
    if (fs->version == 7U)
    {
        for (uint8_t i = 0U; i <= (uint8_t)MODE_QUA_THE; i++)
        {
            if (ctx->mode_cfg[i].toc_do_quat == 20U)
            {
                ctx->mode_cfg[i].toc_do_quat = MODE_DEFAULT_FAN_PERCENT;
            }
        }
    }

    /* Giá trị flash lỗi / chưa ghi (0xFF) → mặc định 100% */
    for (uint8_t i = 0U; i <= (uint8_t)MODE_QUA_THE; i++)
    {
        if (ctx->mode_cfg[i].toc_do_quat > 100U)
        {
            ctx->mode_cfg[i].toc_do_quat = MODE_DEFAULT_FAN_PERCENT;
        }
    }

    /* v9: nạp dữ liệu học tốc độ quạt (v7/v8 không có → giữ mặc định 0) */
    if (fs->version == (uint8_t)APP_SETTINGS_VERSION)
    {
        memcpy(ctx->fan_learned_tach, fs->fan_learned_tach,
               sizeof(ctx->fan_learned_tach));
        ctx->fan_learn_done = fs->fan_learn_done;
    }

    flash_log("[FLASH] Load settings OK");
}

void app_settings_save(app_menu_ctx_t *ctx)
{
    /* Chuẩn bị dữ liệu ghi */
    app_settings_flash_t s;
    memset(&s, 0xFFU, sizeof(s));   /* Flash default state = 0xFF */

    flash_log("[FLASH] Save settings to Flash...");

    s.magic       = APP_SETTINGS_MAGIC;
    s.version     = APP_SETTINGS_VERSION;
    s.active_mode = (uint8_t)ctx->active_mode;
    memcpy(s.mode_cfg, ctx->mode_cfg, sizeof(s.mode_cfg));
    memcpy(s.fan_learned_tach, ctx->fan_learned_tach, sizeof(s.fan_learned_tach));
    s.fan_learn_done = ctx->fan_learn_done;
    s._pad_fan       = 0U;
    s.checksum    = calc_checksum(&s);

    /* ---- Ghi Flash ---- */
    HAL_FLASH_Unlock();

    /* Xóa page 62 */
    FLASH_EraseInitTypeDef erase = {
        .TypeErase   = FLASH_TYPEERASE_PAGES,
        .PageAddress = APP_SETTINGS_FLASH_ADDR,
        .NbPages     = 1U,
    };
    uint32_t page_error = 0U;
    if (HAL_FLASHEx_Erase(&erase, &page_error) != HAL_OK)
    {
        HAL_FLASH_Lock();
        flash_log("[FLASH] Save settings FAILED (erase)");
        return; /* Erase thất bại, không ghi tiếp */
    }

    /* Ghi theo halfword (16-bit) — yêu cầu của STM32F1 */
    const uint16_t *src  = (const uint16_t *)&s;
    uint32_t        addr = APP_SETTINGS_FLASH_ADDR;
    const size_t    n    = sizeof(s) / 2U;
    bool write_ok = true;

    for (size_t i = 0U; i < n; i++)
    {
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, addr, src[i]) != HAL_OK)
        {
            write_ok = false;
            break; /* Ghi thất bại — dữ liệu không đầy đủ, checksum sẽ fail khi load */
        }
        addr += 2U;
    }

    HAL_FLASH_Lock();

    if (write_ok)
    {
        ctx->settings_dirty = false;
        flash_log("[FLASH] Save settings OK");
    }
    else
    {
        flash_log("[FLASH] Save settings FAILED (write)");
    }
}
