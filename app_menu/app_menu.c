/*
 * app_menu.c
 *
 * Toàn bộ logic menu: navigation stack, render, event handler.
 */

#include "app_menu.h"
#include "app_settings.h"
#include "lcd.h"
#include <string.h>
#include <stdio.h>

/* =========================================================================
 * LCD line helper: tự động pad (nếu < 16) hoặc scroll (nếu > 16)
 * Gọi mỗi 50ms từ TaskLCD → tốc độ scroll ~300ms/bước
 * ========================================================================= */

static uint8_t s_scroll_tick = 0U;

/**
 * @brief Gửi 1 dòng ra LCD (16 ký tự).
 *        - Chuỗi <= 16 ký tự: tự động pad khoảng trắng ở cuối.
 *        - Chuỗi > 16 ký tự: cuộn từ phải sang trái (~300 ms/bước).
 */
static void lcd_send_line(const char *str)
{
    char buf[17];
    uint8_t len = (uint8_t)strlen(str);

    if (len <= 16U)
    {
        memcpy(buf, str, len);
        memset(buf + len, ' ', 16U - len);
        buf[16] = '\0';
    }
    else
    {
        /* Cuộn: cứ 6 tick (6 × 50ms = 300ms) mới dịch 1 ký tự */
        uint8_t gap    = 4U;                         /* khoảng trắng giữa 2 vòng lặp */
        uint8_t cycle  = (uint8_t)(len + gap);       /* độ dài 1 chu kỳ cuộn          */
        uint8_t pos    = (uint8_t)((s_scroll_tick / 6U) % cycle);
        /* Xây chuỗi mở rộng: str + "    " + 16 ký tự đầu của str (đủ để wrap) */
        char ext[64];
        if (len + gap + 16U < sizeof(ext))
        {
            memcpy(ext, str, len);
            memset(ext + len, ' ', gap);
            memcpy(ext + len + gap, str, 16U);
            ext[len + gap + 16U] = '\0';
        }
        else
        {
            /* chuỗi quá dài, cắt thẳng */
            memcpy(ext, str, sizeof(ext) - 1U);
            ext[sizeof(ext) - 1U] = '\0';
        }
        memcpy(buf, ext + pos, 16U);
        buf[16] = '\0';
    }

    lcd_send_string(buf);
}

/* =========================================================================
 * String tables
 * ========================================================================= */

static const char * const g_main_menu_items[] = {
    "Chon che do",
    "Cai dat t/gian",
    "Cai dat MinMax",
    "Vi tri DS18B20",
};
#define MAIN_MENU_COUNT   4U

static const char * const g_mode_items[] = {
    "Chay to",
    "Kich dinh ghim",
    "Dinh ghim",
    "Qua the",
    "Thanh trung",
    "Nghi",
};
#define MODE_ITEM_COUNT   6U

static const char * const g_time_items[] = {
    "Set Year",
    "Set Month",
    "Set Day",
    "Set Hour",
    "Set Minute",
    "Set Second",
};
#define TIME_ITEM_COUNT   6U

static const char * const g_minmax_mode_items[] = {
    "Chay to",
    "Kich dinh ghim",
    "Dinh ghim",
    "Qua the",
    "Thanh trung",
};
#define MINMAX_MODE_COUNT 5U

static const char * const g_minmax_param_items[] = {
    "Nhiet do",
    "Do am",
    "Nong do CO2",
    "Den",
    "Toc do quat",
};
#define MINMAX_PARAM_COUNT 5U

static const char * const g_thanh_trung_param_items[] = {
    "Nhiet do",
    "Thoi gian BD",
    "Mo muc to",
    "Mo muc nho",
};
#define THANH_TRUNG_PARAM_COUNT 4U

#define TT_PARAM_NHIET_DO     0U
#define TT_PARAM_TIME_BD      1U
#define TT_PARAM_SG90_MO_TO   2U
#define TT_PARAM_SG90_MO_NHO  3U

static const char * const g_field_minmax[] = { "Min", "Max" };
/* Đèn - cấp 1: chọn chế độ đèn */
static const char * const g_field_den[]      = { "Su dung", "Khong su dung", "24/24" };
/* Đèn - cấp 2: khi chọn Su dung → chỉnh thời gian */
static const char * const g_field_den_time[] = { "Time Start", "Time Stop" };
/* Độ ẩm - cấp 1: chọn chế độ */
static const char * const g_field_do_am[]    = { "Su dung", "Khong su dung" };

/* g_mode_short removed - replaced by g_mode_char in render_work1/2 */

/**
 * @brief Trả về số thông số cài đặt của chế độ đang chọn.
 * (Thanh trùng chỉ có 1 thông số: Nhiệt độ)
 */
static uint8_t get_mode_param_count(const app_menu_ctx_t *ctx)
{
    return (ctx->edit_mode_index == 4U) ? (uint8_t)THANH_TRUNG_PARAM_COUNT : (uint8_t)MINMAX_PARAM_COUNT;
}

/**
 * @brief Làm tròn số nguyên dương: round(x / divisor)
 * Hỗ trợ cả âm: -73.5 deciC -> -74 (làm tròn ra xa 0)
 */
static inline int32_t round_div(int32_t x, int32_t divisor)
{
    /* Cộng/trừ nửa divisor trước khi chia để làm tròn */
    if (x >= 0)
        return (x + divisor / 2) / divisor;
    else
        return (x - divisor / 2) / divisor;
}

/* =========================================================================
 * Navigation helpers
 * ========================================================================= */

/**
 * @brief Đẩy màn hình hiện tại vào stack và chuyển sang màn hình mới.
 */
static void nav_push(app_menu_ctx_t *ctx, app_screen_t new_screen)
{
    if (ctx->stack_top < MENU_NAV_DEPTH - 1U)
    {
        ctx->stack[ctx->stack_top].screen = ctx->screen;
        ctx->stack[ctx->stack_top].cursor = ctx->cursor;
        ctx->stack[ctx->stack_top].scroll  = ctx->scroll;
        ctx->stack_top++;
    }
    ctx->screen = new_screen;
    ctx->cursor = 0U;
    ctx->scroll  = 0U;
    ctx->dirty   = true;
}

/**
 * @brief Lấy màn hình trước từ stack (quay lại).
 */
static void nav_pop(app_menu_ctx_t *ctx)
{
    if (ctx->stack_top > 0U)
    {
        ctx->stack_top--;
        ctx->screen = ctx->stack[ctx->stack_top].screen;
        ctx->cursor = ctx->stack[ctx->stack_top].cursor;
        ctx->scroll  = ctx->stack[ctx->stack_top].scroll;
        ctx->dirty   = true;
    }
}

/* =========================================================================
 * List / Edit motion helpers
 * ========================================================================= */

/** @brief Di chuyển cursor xuống (CW), cuộn nếu cần. */
static void list_cw(app_menu_ctx_t *ctx, uint8_t count)
{
    if (ctx->cursor < (uint8_t)(count - 1U))
    {
        ctx->cursor++;
        if (ctx->cursor > (uint8_t)(ctx->scroll + 1U))
        {
            ctx->scroll++;
        }
        ctx->dirty = true;
    }
}

/** @brief Di chuyển cursor lên (CCW), cuộn nếu cần. */
static void list_ccw(app_menu_ctx_t *ctx)
{
    if (ctx->cursor > 0U)
    {
        ctx->cursor--;
        if (ctx->cursor < ctx->scroll)
        {
            ctx->scroll--;
        }
        ctx->dirty = true;
    }
}

/** @brief Tăng giá trị chỉnh sửa (CW). */
static void edit_cw(app_menu_ctx_t *ctx)
{
    if (ctx->edit_value < ctx->edit_max)
    {
        ctx->edit_value++;
        ctx->dirty = true;
    }
}

/** @brief Giảm giá trị chỉnh sửa (CCW). */
static void edit_ccw(app_menu_ctx_t *ctx)
{
    if (ctx->edit_value > ctx->edit_min)
    {
        ctx->edit_value--;
        ctx->dirty = true;
    }
}

/* =========================================================================
 * LCD render helpers
 * ========================================================================= */

/**
 * @brief Hiển thị menu kiểu: Dòng 0 = tiêu đề, Dòng 1 = item đang chọn.
 *
 * Khi xoay encoder, cursor thay đổi → dòng 1 cập nhật sang item mới.
 */
static void render_title_item(const char *title,
                               const char * const items[],
                               uint8_t cursor)
{
    char line[17];

    /* Dòng 0: tiêu đề cố định */
    snprintf(line, sizeof(line), "%-16s", title);
    line[16] = '\0';
    lcd_put_cur(0, 0);
    lcd_send_string(line);

    /* Dòng 1: '>' + item hiện tại (theo cursor) */
    snprintf(line, sizeof(line), ">%-15s", items[cursor]);
    line[16] = '\0';
    lcd_put_cur(1, 0);
    lcd_send_string(line);
}

/**
 * @brief Hiển thị màn hình chỉnh sửa giá trị số.
 *
 * Dòng 0: tên trường.
 * Dòng 1: < value > (mũi tên ẩn khi đạt giới hạn).
 */
static void render_edit(const char *field_name,
                         int32_t value,
                         int32_t vmin,
                         int32_t vmax)
{
    char line[32];

    /* Dòng 0: tên trường */
    snprintf(line, sizeof(line), "%-16s", field_name);
    line[16] = '\0';
    lcd_put_cur(0, 0);
    lcd_send_string(line);

    /* Dòng 1: giá trị với mũi tên */
    char left  = (value > vmin) ? '<' : ' ';
    char right = (value < vmax) ? '>' : ' ';
    snprintf(line, sizeof(line), "%c %6ld       %c", left, value, right);
    line[16] = '\0';
    lcd_put_cur(1, 0);
    lcd_send_string(line);
}

/* =========================================================================
 * Screen render functions
 * ========================================================================= */

/* Ký tự viết tắt cho từng chế độ vận hành */
static const char g_mode_char[] = { 'C', 'K', 'D', 'Q', 'T', 'N' };

/** @brief Tốc độ quạt hiển thị trên màn hình làm việc (0% khi Nghỉ / Thanh trùng). */
static uint8_t get_fan_display_percent(const app_menu_ctx_t *ctx)
{
    if ((ctx->active_mode == MODE_NGHI) || (ctx->active_mode == MODE_THANH_TRUNG))
    {
        return 0U;
    }
    if (ctx->active_mode <= MODE_QUA_THE)
    {
        return ctx->mode_cfg[ctx->active_mode].toc_do_quat;
    }
    return 0U;
}

static void render_work1(app_menu_ctx_t *ctx)
{
    /* Buffer 48 bytes: đủ cho worst case snprintf dòng 1 (~39 chars) */
    char line[48];

    /* ---- Dòng 0: [Mode] [HH:MM] [DD/MM/YY] ----
     * Ví dụ: "C 19:50 30/12/26"  (16 chars)
     */
    char mode_c = g_mode_char[ctx->active_mode];
    snprintf(line, sizeof(line), "%c %02u:%02u %02u/%02u/%02u",
             mode_c,
             ctx->time_cfg.hour,   ctx->time_cfg.minute,
             ctx->time_cfg.day,    ctx->time_cfg.month,
             (uint8_t)(ctx->time_cfg.year % 100U));
    line[16] = '\0';
    lcd_put_cur(0, 0);
    lcd_send_string(line);

    /* ---- Dòng 1: T[avg°C] A[%RH] C[ppm] + Qxx tại cột 13 ----
     * Nhiệt độ: trung bình các cảm biến hợp lệ (không lỗi, đã có data).
     * Nếu tất cả đều lỗi → hiển thị "ERR" thay vì số.
     */
    int32_t avg_t   = 0;
    uint8_t avg_cnt = 0U;
    uint8_t target  = ctx->ds18b20_target_count;
    if (target == 0U) target = ctx->ds18b20_count;   /* fallback nếu chưa cài */

    for (uint8_t i = 0U; i < target && i < MENU_DS18B20_MAX; i++)
    {
        /* Bỏ qua sensor có fault hoặc chưa có data */
        if (ctx->ds18b20_fault_mask & (uint8_t)(1U << i)) continue;
        if (ctx->ds18b20[i].tick == 0U) continue;
        avg_t += (int32_t)ctx->ds18b20[i].tempDeciC;
        avg_cnt++;
    }
    if (avg_cnt > 0U)
    {
        avg_t = round_div(avg_t / (int32_t)avg_cnt, 10);
    }

    /* m%RH -> %RH, làm tròn */
    int32_t humi = round_div(ctx->scd41.humidity_m_percent_rh, 1000L);

    lcd_put_cur(1, 0);

    /* Hiển thị ERR cho từng nguồn lỗi:
     *   TERR = DS18B20 lỗi   AERR = SCD41 lỗi (độ ẩm)   CERR = SCD41 lỗi (CO2)
     * Kết hợp các trạng thái lỗi vào 1 dòng 16 ký tự. */
    bool t_err = (ctx->ds18b20_fault_mask != 0U);
    bool ac_err = ctx->scd41_fault;

    if (t_err && ac_err)
    {
        snprintf(line, sizeof(line), "TERR AERR CERR");
    }
    else if (t_err)
    {
        snprintf(line, sizeof(line), "TERR A%ld C%u", humi, ctx->scd41.co2);
    }
    else if (ac_err)
    {
        snprintf(line, sizeof(line), "T%ld AERR CERR", avg_t);
    }
    else
    {
        snprintf(line, sizeof(line), "T%ld A%ld C%u", avg_t, humi, ctx->scd41.co2);
    }
    lcd_send_line(line);

    {
        char qfan[4];
        uint8_t pct = get_fan_display_percent(ctx);
        if (pct >= 100U)
        {
            snprintf(qfan, sizeof(qfan), "100");
        }
        else
        {
            snprintf(qfan, sizeof(qfan), "Q%02u", (unsigned)pct);
        }
        lcd_put_cur(1, 13);
        lcd_send_string(qfan);
    }
}

static void render_work2(app_menu_ctx_t *ctx)
{
    char line[32];
    char slot[3][8];
    uint8_t target = ctx->ds18b20_target_count;
    if (target == 0U) target = ctx->ds18b20_count;   /* fallback */
    if (target > MENU_DS18B20_MAX) target = MENU_DS18B20_MAX;

    /* ---- Dòng 0: vị trí 1, 2, 3 (index 0,1,2) ---- */
    for (uint8_t i = 0U; i < 3U; i++)
    {
        uint8_t fault = (ctx->ds18b20_fault_mask >> i) & 1U;
        if (i >= target)
        {
            slot[i][0] = '\0';
        }
        else if (fault)
        {
            snprintf(slot[i], sizeof(slot[i]), "%u:ERR", (unsigned)(i + 1U));
        }
        else if (ctx->ds18b20[i].tick != 0U)
        {
            int16_t t = (int16_t)round_div((int32_t)ctx->ds18b20[i].tempDeciC, 10);
            snprintf(slot[i], sizeof(slot[i]), "%u:%d", (unsigned)(i + 1U), (int)t);
        }
        else
        {
            snprintf(slot[i], sizeof(slot[i]), "%u:--", (unsigned)(i + 1U));
        }
    }
    snprintf(line, sizeof(line), "%-5s%-5s%-6s", slot[0], slot[1], slot[2]);
    line[16] = '\0';
    lcd_put_cur(0, 0);
    lcd_send_string(line);

    /* ---- Dòng 1: vị trí 4, 5, 6 (index 3,4,5) — chỉ nếu target > 3 ---- */
    for (uint8_t i = 3U; i < 6U; i++)
    {
        uint8_t s     = (uint8_t)(i - 3U);
        uint8_t fault = (ctx->ds18b20_fault_mask >> i) & 1U;
        if (i >= target)
        {
            slot[s][0] = '\0';
        }
        else if (fault)
        {
            snprintf(slot[s], sizeof(slot[s]), "%u:ERR", (unsigned)(i + 1U));
        }
        else if (ctx->ds18b20[i].tick != 0U)
        {
            int16_t t = (int16_t)round_div((int32_t)ctx->ds18b20[i].tempDeciC, 10);
            snprintf(slot[s], sizeof(slot[s]), "%u:%d", (unsigned)(i + 1U), (int)t);
        }
        else
        {
            snprintf(slot[s], sizeof(slot[s]), "%u:--", (unsigned)(i + 1U));
        }
    }
    snprintf(line, sizeof(line), "%-5s%-5s%-6s", slot[0], slot[1], slot[2]);
    line[16] = '\0';
    lcd_put_cur(1, 0);
    lcd_send_string(line);
}

/**
 * @brief Màn hình làm việc 3: hiển thị ngưỡng Min-Max của chế độ đang chạy.
 *
 * Chế độ thường (Chay to / Dinh ghim / Qua the):
 *   Dòng 0: "[M] T[min]-[max] A[min]-[max]"  (16 chars)
 *   Dòng 1: "C[min]-[max] D..." — CO2 bắt đầu cột 0 (vd. C1200-5000)
 *
 * Chế độ Thanh trùng (chỉ có nhiệt độ):
 *   Dòng 0: "[M] T[min]-[max]              "
 *   Dòng 1: "                              "
 *
 * Chế độ Nghi: không có MinMax → hiển thị "---"
 */
static void render_work3(app_menu_ctx_t *ctx)
{
    char line[64];   /* 64 bytes: đủ cho worst-case snprintf với nhiều %d */
    char prefix[32];
    char mode_c = g_mode_char[ctx->active_mode];

    if (ctx->active_mode == MODE_NGHI)
    {
        /* Chế độ Nghỉ: không có cài đặt MinMax */
        snprintf(line, sizeof(line), "%c ---           ", mode_c);
        line[16] = '\0';
        lcd_put_cur(0, 0);
        lcd_send_string(line);
        lcd_put_cur(1, 0);
        lcd_send_string("                ");
        return;
    }

    /* Index chế độ (0-4 tương ứng Chay to / Kich Dinh ghim / Dinh ghim / Qua the / Thanh trung) */
    uint8_t m = (uint8_t)ctx->active_mode;
    if (m >= 5U) m = 0U;   /* bảo vệ */
    const mode_settings_t *cfg = &ctx->mode_cfg[m];

    if (ctx->active_mode == MODE_THANH_TRUNG)
    {
        /* Thanh trùng:
         * Dòng 0: BD luôn bắt đầu tại cột 10 (index 9)
         * Dòng 1: "   To120  Nho110"
         */
        snprintf(line, sizeof(line), "%c  T%d-%d",
                 mode_c,
                 (int)cfg->nhiet_do.min,
                 (int)cfg->nhiet_do.max);
        snprintf(prefix, sizeof(prefix), "%-10.10s", line);
        snprintf(line, sizeof(line), "%sBD%u",
                 prefix,
                 (unsigned)cfg->thanh_trung_initial_minutes);
        lcd_put_cur(0, 0);
        lcd_send_line(line);

        snprintf(line, sizeof(line), "   To%u  Nho%u",
                 (unsigned)cfg->sg90_mo_to_deg,
                 (unsigned)cfg->sg90_mo_nho_deg);
        lcd_put_cur(1, 0);
        lcd_send_line(line);
    }
    else
    {
        /* Các chế độ còn lại: T, A trên dòng 0; C + Den trên dòng 1 */
        if (cfg->do_am_disabled != 0U)
        {
            snprintf(line, sizeof(line), "%c T%d-%d A------",
                     mode_c,
                     (int)cfg->nhiet_do.min, (int)cfg->nhiet_do.max);
        }
        else
        {
            snprintf(line, sizeof(line), "%c T%d-%d A%d-%d  ",
                     mode_c,
                     (int)cfg->nhiet_do.min, (int)cfg->nhiet_do.max,
                     (int)cfg->do_am.min,    (int)cfg->do_am.max);
        }
        line[16] = '\0';
        lcd_put_cur(0, 0);
        lcd_send_string(line);

        /* Dòng 1: CO2 tại cột 0 (C1200-5000), sau đó là Đèn nếu còn chỗ */
        snprintf(line, sizeof(line), "C%d-%d",
                 (int)cfg->co2.min, (int)cfg->co2.max);
        if (cfg->den.den_mode == DEN_MODE_KHONG_SU_DUNG)
        {
            (void)snprintf(line + strlen(line), sizeof(line) - strlen(line), " D-----");
        }
        else if (cfg->den.den_mode == DEN_MODE_24_24)
        {
            (void)snprintf(line + strlen(line), sizeof(line) - strlen(line), " D24/24");
        }
        else
        {
            (void)snprintf(line + strlen(line), sizeof(line) - strlen(line),
                           " D%02u-%02u",
                           (unsigned)cfg->den.time_start_h,
                           (unsigned)cfg->den.time_stop_h);
        }
        lcd_put_cur(1, 0);
        lcd_send_line(line);
    }
}

static void render_main_menu(app_menu_ctx_t *ctx)
{
    render_title_item("< Menu >", g_main_menu_items, ctx->cursor);
}

static void render_mode_select(app_menu_ctx_t *ctx)
{
    render_title_item("Chon che do", g_mode_items, ctx->cursor);
}

static void render_time_menu(app_menu_ctx_t *ctx)
{
    render_title_item("Cai dat t/gian", g_time_items, ctx->cursor);
}

static void render_time_edit(app_menu_ctx_t *ctx)
{
    render_edit(g_time_items[ctx->edit_field_index],
                ctx->edit_value,
                ctx->edit_min,
                ctx->edit_max);
}

static void render_minmax_mode(app_menu_ctx_t *ctx)
{
    render_title_item("Cai dat MinMax", g_minmax_mode_items, ctx->cursor);
}

static void render_minmax_param(app_menu_ctx_t *ctx)
{
    /* Dòng 0: tên chế độ đang cài (ví dụ "Chay to") */
    render_title_item(g_minmax_mode_items[ctx->edit_mode_index],
                      (ctx->edit_mode_index == 4U) ? g_thanh_trung_param_items : g_minmax_param_items,
                      ctx->cursor);
}

static void render_minmax_field(app_menu_ctx_t *ctx)
{
    /* Dòng 0: tên thông số đang cài */
    if (ctx->edit_param_index == (uint8_t)PARAM_DO_AM)
    {
        if (ctx->do_am_field_level == 0U)
        {
            /* Cấp 1: chọn Su dung / Khong su dung */
            render_title_item("Do am", g_field_do_am, ctx->cursor);
        }
        else
        {
            /* Cấp 2: chỉnh Min / Max */
            render_title_item("Do am:Su dung", g_field_minmax, ctx->cursor);
        }
    }
    else if (ctx->edit_param_index == (uint8_t)PARAM_DEN)
    {
        if (ctx->den_field_level == 0U)
        {
            /* Cấp 1: chọn chế độ đèn */
            render_title_item("Den", g_field_den, ctx->cursor);
        }
        else
        {
            /* Cấp 2: Su dung → chỉnh thời gian bật/tắt */
            render_title_item("Den:Su dung", g_field_den_time, ctx->cursor);
        }
    }
    else if (ctx->edit_mode_index == 4U)
    {
        /* Thanh trung: chỉ Nhiet do mới có Min/Max */
        render_title_item(g_thanh_trung_param_items[ctx->edit_param_index],
                          g_field_minmax, ctx->cursor);
    }
    else
    {
        render_title_item(g_minmax_param_items[ctx->edit_param_index],
                          g_field_minmax, ctx->cursor);
    }
}

static void render_minmax_edit(app_menu_ctx_t *ctx)
{
    char field_name[17];
    if (ctx->edit_mode_index == 4U)
    {
        if (ctx->edit_param_index == TT_PARAM_TIME_BD)
        {
            snprintf(field_name, sizeof(field_name), "Thoi gian BD");
        }
        else if (ctx->edit_param_index == TT_PARAM_SG90_MO_TO)
        {
            snprintf(field_name, sizeof(field_name), "Mo muc to");
        }
        else if (ctx->edit_param_index == TT_PARAM_SG90_MO_NHO)
        {
            snprintf(field_name, sizeof(field_name), "Mo muc nho");
        }
        else
        {
            snprintf(field_name, sizeof(field_name), "%.8s %.3s",
                     g_thanh_trung_param_items[ctx->edit_param_index],
                     g_field_minmax[ctx->edit_field_index]);
        }
    }
    else if (ctx->edit_param_index == (uint8_t)PARAM_DEN)
    {
        /* Den: chỉ vào đây khi Su dung → Time Start (0) hoặc Time Stop (1) */
        snprintf(field_name, sizeof(field_name), "%.16s",
                 g_field_den_time[ctx->edit_field_index]);
    }
    else if (ctx->edit_param_index == (uint8_t)PARAM_TOC_DO_QUAT)
    {
        snprintf(field_name, sizeof(field_name), "Toc do quat");
    }
    else
    {
        snprintf(field_name, sizeof(field_name), "%.8s %.3s",
                 g_minmax_param_items[ctx->edit_param_index],
                 g_field_minmax[ctx->edit_field_index]);
    }

    render_edit(field_name, ctx->edit_value, ctx->edit_min, ctx->edit_max);
}

static const char * const g_ds18b20_pos_items[] = {
    "So cam bien",
    "Hoc vi tri",
};
#define DS18B20_POS_ITEM_COUNT 2U

static void render_ds18b20_pos(app_menu_ctx_t *ctx)
{
    render_title_item("Vi tri DS18B20", g_ds18b20_pos_items, ctx->cursor);
}

static void render_ds18b20_count(app_menu_ctx_t *ctx)
{
    render_edit("So cam bien", ctx->edit_value, ctx->edit_min, ctx->edit_max);
}

static void render_ds18b20_learn(app_menu_ctx_t *ctx)
{
    char tmp[32];   /* Buffer lớn hơn LCD để snprintf không cắt → hết warning */

    s_scroll_tick++;  /* Tăng tick cuộn mỗi lần render (50ms) */

    switch (ctx->relearn_phase)
    {
    case DS18B20_LEARN_SEARCHING:
        if (ctx->relearn_current_pos == 0U)
        {
            lcd_put_cur(0, 0);
            lcd_send_line("Hoc vi tri...");
            lcd_put_cur(1, 0);
            lcd_send_line("Chuan bi...");
        }
        else if (ctx->relearn_pos_found == 0U)
        {
            /* Đang yêu cầu người dùng hơ nóng vị trí N */
            lcd_put_cur(0, 0);
            lcd_send_line("Ho nong vi tri");
            snprintf(tmp, sizeof(tmp), "so %u/%u",
                     (unsigned)ctx->relearn_current_pos,
                     (unsigned)ctx->ds18b20_target_count);
            lcd_put_cur(1, 0);
            lcd_send_line(tmp);
        }
        else
        {
            /* Vừa tìm thấy vị trí N */
            snprintf(tmp, sizeof(tmp), "Tim thay so: %u",
                     (unsigned)ctx->relearn_current_pos);
            lcd_put_cur(0, 0);
            lcd_send_line(tmp);
            lcd_put_cur(1, 0);
            lcd_send_line("Tiep theo...");
        }
        break;

    case DS18B20_LEARN_DONE:
        lcd_put_cur(0, 0);
        lcd_send_line("Hoc xong!");
        snprintf(tmp, sizeof(tmp), "Tim duoc %u CB",
                 (unsigned)ctx->ds18b20_target_count);
        lcd_put_cur(1, 0);
        lcd_send_line(tmp);
        break;

    case DS18B20_LEARN_ERROR:
    default:
        lcd_put_cur(0, 0);
        lcd_send_line("Khong du CB!");
        lcd_put_cur(1, 0);
        lcd_send_line("SW ngan=thoat");
        break;
    }
}

/* =========================================================================
 * Time edit helpers
 * ========================================================================= */

static const int32_t g_time_edit_min[] = { 2020, 1,  1,  0,  0,  0 };
static const int32_t g_time_edit_max[] = { 2099, 12, 31, 23, 59, 59 };

static void enter_time_edit(app_menu_ctx_t *ctx, uint8_t field)
{
    const int32_t vals[] = {
        (int32_t)ctx->time_cfg.year,
        (int32_t)ctx->time_cfg.month,
        (int32_t)ctx->time_cfg.day,
        (int32_t)ctx->time_cfg.hour,
        (int32_t)ctx->time_cfg.minute,
        (int32_t)ctx->time_cfg.second,
    };
    ctx->edit_field_index = field;
    ctx->edit_min         = g_time_edit_min[field];
    ctx->edit_max         = g_time_edit_max[field];
    ctx->edit_value       = vals[field];
    nav_push(ctx, SCREEN_TIME_EDIT);
}

static void save_time_edit(app_menu_ctx_t *ctx)
{
    switch (ctx->edit_field_index)
    {
    case 0U: ctx->time_cfg.year   = (uint16_t)ctx->edit_value; break;
    case 1U: ctx->time_cfg.month  = (uint8_t)ctx->edit_value;  break;
    case 2U: ctx->time_cfg.day    = (uint8_t)ctx->edit_value;  break;
    case 3U: ctx->time_cfg.hour   = (uint8_t)ctx->edit_value;  break;
    case 4U: ctx->time_cfg.minute = (uint8_t)ctx->edit_value;  break;
    case 5U: ctx->time_cfg.second = (uint8_t)ctx->edit_value;  break;
    default: break;
    }
    /* Đánh dấu để TaskUI ghi lại vào RTC */
    ctx->time_rtc_dirty = true;
}

/* =========================================================================
 * MinMax edit helpers
 * ========================================================================= */

static void get_minmax_range(app_menu_ctx_t *ctx,
                               int32_t *vmin, int32_t *vmax)
{
    if (ctx->edit_mode_index == 4U)
    {
        switch (ctx->edit_param_index)
        {
        case TT_PARAM_NHIET_DO:
            *vmin = 20;
            *vmax = 100;
            break;
        case TT_PARAM_TIME_BD:
            *vmin = 0;
            *vmax = 600; /* phút */
            break;
        case TT_PARAM_SG90_MO_TO:
        case TT_PARAM_SG90_MO_NHO:
            *vmin = 0;
            *vmax = 180;
            break;
        default:
            *vmin = 0;
            *vmax = 100;
            break;
        }
        return;
    }

    switch ((minmax_param_t)ctx->edit_param_index)
    {
    case PARAM_NHIET_DO:
        *vmin = 20;
        *vmax = (ctx->edit_mode_index == 4U) ? 100 : 35;
        break;
    case PARAM_DO_AM:    *vmin = 50;    *vmax = 95;    break; /* %RH     */
    case PARAM_CO2:      *vmin = 400;   *vmax = 5000;  break; /* ppm     */
    case PARAM_DEN:
        /* Chỉ Time Start (0) và Time Stop (1) mới đến SCREEN_MINMAX_EDIT */
        *vmin = 0;
        *vmax = 23;   /* giờ 0-23 */
        break;
    case PARAM_TOC_DO_QUAT:
        *vmin = 0;
        *vmax = 100;  /* % tốc độ quạt */
        break;
    default:             *vmin = 0;     *vmax = 100;   break;
    }
}

static int32_t get_minmax_value(app_menu_ctx_t *ctx)
{
    mode_settings_t *cfg = &ctx->mode_cfg[ctx->edit_mode_index];

    if (ctx->edit_mode_index == 4U)
    {
        switch (ctx->edit_param_index)
        {
        case TT_PARAM_NHIET_DO:
            return (ctx->edit_field_index == 0U) ?
                   (int32_t)cfg->nhiet_do.min : (int32_t)cfg->nhiet_do.max;
        case TT_PARAM_TIME_BD:
            return (int32_t)cfg->thanh_trung_initial_minutes;
        case TT_PARAM_SG90_MO_TO:
            return (int32_t)cfg->sg90_mo_to_deg;
        case TT_PARAM_SG90_MO_NHO:
            return (int32_t)cfg->sg90_mo_nho_deg;
        default:
            return 0;
        }
    }

    switch ((minmax_param_t)ctx->edit_param_index)
    {
    case PARAM_NHIET_DO:
        return (ctx->edit_field_index == 0U) ?
               (int32_t)cfg->nhiet_do.min : (int32_t)cfg->nhiet_do.max;
    case PARAM_DO_AM:
        return (ctx->edit_field_index == 0U) ?
               (int32_t)cfg->do_am.min : (int32_t)cfg->do_am.max;
    case PARAM_CO2:
        return (ctx->edit_field_index == 0U) ?
               (int32_t)cfg->co2.min : (int32_t)cfg->co2.max;
    case PARAM_DEN:
        /* field 0 = Time Start, field 1 = Time Stop */
        if (ctx->edit_field_index == 0U)
            return (int32_t)cfg->den.time_start_h;
        return (int32_t)cfg->den.time_stop_h;
    case PARAM_TOC_DO_QUAT:
        return (int32_t)cfg->toc_do_quat;
    default:
        return 0;
    }
}

static void save_minmax_value(app_menu_ctx_t *ctx)
{
    mode_settings_t *cfg = &ctx->mode_cfg[ctx->edit_mode_index];

    if (ctx->edit_mode_index == 4U)
    {
        switch (ctx->edit_param_index)
        {
        case TT_PARAM_NHIET_DO:
            if (ctx->edit_field_index == 0U) cfg->nhiet_do.min = (int16_t)ctx->edit_value;
            else                             cfg->nhiet_do.max = (int16_t)ctx->edit_value;
            break;
        case TT_PARAM_TIME_BD:
            cfg->thanh_trung_initial_minutes = (uint16_t)ctx->edit_value;
            break;
        case TT_PARAM_SG90_MO_TO:
            cfg->sg90_mo_to_deg = (uint8_t)ctx->edit_value;
            break;
        case TT_PARAM_SG90_MO_NHO:
            cfg->sg90_mo_nho_deg = (uint8_t)ctx->edit_value;
            break;
        default:
            break;
        }
        ctx->settings_dirty = true;
        return;
    }

    switch ((minmax_param_t)ctx->edit_param_index)
    {
    case PARAM_NHIET_DO:
        if (ctx->edit_field_index == 0U) cfg->nhiet_do.min = (int16_t)ctx->edit_value;
        else                             cfg->nhiet_do.max = (int16_t)ctx->edit_value;
        break;
    case PARAM_DO_AM:
        if (ctx->edit_field_index == 0U) cfg->do_am.min = (int16_t)ctx->edit_value;
        else                             cfg->do_am.max = (int16_t)ctx->edit_value;
        break;
    case PARAM_CO2:
        if (ctx->edit_field_index == 0U) cfg->co2.min = (int16_t)ctx->edit_value;
        else                             cfg->co2.max = (int16_t)ctx->edit_value;
        break;
    case PARAM_DEN:
        /* field 0 = Time Start, field 1 = Time Stop */
        if (ctx->edit_field_index == 0U)
        {
            cfg->den.time_start_h = (uint8_t)ctx->edit_value;
            cfg->den.time_start_m = 0U;
        }
        else if (ctx->edit_field_index == 1U)
        {
            cfg->den.time_stop_h = (uint8_t)ctx->edit_value;
            cfg->den.time_stop_m = 0U;
        }
        break;
    case PARAM_TOC_DO_QUAT:
        cfg->toc_do_quat = (uint8_t)ctx->edit_value;
        break;
    default:
        break;
    }
    /* MinMax thay đổi, cần lưu Flash khi thoát menu */
    ctx->settings_dirty = true;
}

/* =========================================================================
 * Per-screen event handlers
 * ========================================================================= */

static void handle_work1(app_menu_ctx_t *ctx, rtrecd_queue_item_t ev)
{
    switch (ev)
    {
    case RTRECD_EVENT_ROTATE_CW:
        ctx->screen = SCREEN_WORK2;
        ctx->dirty  = true;
        break;
    case RTRECD_EVENT_BUTTON_SHORT:
        nav_push(ctx, SCREEN_MAIN_MENU);
        break;
    default:
        break;
    }
}

static void handle_work2(app_menu_ctx_t *ctx, rtrecd_queue_item_t ev)
{
    switch (ev)
    {
    case RTRECD_EVENT_ROTATE_CCW:
        ctx->screen = SCREEN_WORK1;
        ctx->dirty  = true;
        break;
    case RTRECD_EVENT_ROTATE_CW:
        ctx->screen = SCREEN_WORK3;
        ctx->dirty  = true;
        break;
    case RTRECD_EVENT_BUTTON_LONG:
        ctx->screen = SCREEN_WORK1;
        ctx->dirty  = true;
        break;
    case RTRECD_EVENT_BUTTON_SHORT:
        nav_push(ctx, SCREEN_MAIN_MENU);
        break;
    default:
        break;
    }
}

static void handle_work3(app_menu_ctx_t *ctx, rtrecd_queue_item_t ev)
{
    switch (ev)
    {
    case RTRECD_EVENT_ROTATE_CCW:
        /* fall-through */
    case RTRECD_EVENT_BUTTON_LONG:
        ctx->screen = SCREEN_WORK2;
        ctx->dirty  = true;
        break;
    case RTRECD_EVENT_BUTTON_SHORT:
        nav_push(ctx, SCREEN_MAIN_MENU);
        break;
    default:
        break;
    }
}

static void handle_main_menu(app_menu_ctx_t *ctx, rtrecd_queue_item_t ev)
{
    switch (ev)
    {
    case RTRECD_EVENT_ROTATE_CW:
        list_cw(ctx, MAIN_MENU_COUNT);
        break;
    case RTRECD_EVENT_ROTATE_CCW:
        list_ccw(ctx);
        break;
    case RTRECD_EVENT_BUTTON_SHORT:
        switch (ctx->cursor)
        {
        case 0U: nav_push(ctx, SCREEN_MODE_SELECT); break;
        case 1U: nav_push(ctx, SCREEN_TIME_MENU);   break;
        case 2U: nav_push(ctx, SCREEN_MINMAX_MODE); break;
        case 3U: nav_push(ctx, SCREEN_DS18B20_POS); break;
        default: break;
        }
        break;
    case RTRECD_EVENT_BUTTON_LONG:
        /* Thoát về màn hình làm việc: lưu Flash nếu có thay đổi */
        if (ctx->settings_dirty)
        {
            app_settings_save(ctx);   /* clear settings_dirty bên trong */
        }
        nav_pop(ctx); /* quay về màn hình làm việc */
        break;
    default:
        break;
    }
}

static void handle_mode_select(app_menu_ctx_t *ctx, rtrecd_queue_item_t ev)
{
    switch (ev)
    {
    case RTRECD_EVENT_ROTATE_CW:
        list_cw(ctx, MODE_ITEM_COUNT);
        break;
    case RTRECD_EVENT_ROTATE_CCW:
        list_ccw(ctx);
        break;
    case RTRECD_EVENT_BUTTON_SHORT:
        ctx->active_mode   = (app_mode_t)ctx->cursor;
        ctx->settings_dirty = true;   /* chế độ thay đổi, cần lưu Flash */
        ctx->dirty = true;
        nav_pop(ctx);
        break;
    case RTRECD_EVENT_BUTTON_LONG:
        nav_pop(ctx);
        break;
    default:
        break;
    }
}

static void handle_time_menu(app_menu_ctx_t *ctx, rtrecd_queue_item_t ev)
{
    switch (ev)
    {
    case RTRECD_EVENT_ROTATE_CW:
        list_cw(ctx, TIME_ITEM_COUNT);
        break;
    case RTRECD_EVENT_ROTATE_CCW:
        list_ccw(ctx);
        break;
    case RTRECD_EVENT_BUTTON_SHORT:
        enter_time_edit(ctx, ctx->cursor);
        break;
    case RTRECD_EVENT_BUTTON_LONG:
        nav_pop(ctx);
        break;
    default:
        break;
    }
}

static void handle_time_edit(app_menu_ctx_t *ctx, rtrecd_queue_item_t ev)
{
    switch (ev)
    {
    case RTRECD_EVENT_ROTATE_CW:
        edit_cw(ctx);
        break;
    case RTRECD_EVENT_ROTATE_CCW:
        edit_ccw(ctx);
        break;
    case RTRECD_EVENT_BUTTON_SHORT:
        save_time_edit(ctx);
        nav_pop(ctx); /* xác nhận, về time_menu */
        break;
    case RTRECD_EVENT_BUTTON_LONG:
        nav_pop(ctx); /* huỷ, về time_menu */
        break;
    default:
        break;
    }
}

static void handle_minmax_mode(app_menu_ctx_t *ctx, rtrecd_queue_item_t ev)
{
    switch (ev)
    {
    case RTRECD_EVENT_ROTATE_CW:
        list_cw(ctx, MINMAX_MODE_COUNT);
        break;
    case RTRECD_EVENT_ROTATE_CCW:
        list_ccw(ctx);
        break;
    case RTRECD_EVENT_BUTTON_SHORT:
        ctx->edit_mode_index = ctx->cursor;
        nav_push(ctx, SCREEN_MINMAX_PARAM);
        break;
    case RTRECD_EVENT_BUTTON_LONG:
        nav_pop(ctx);
        break;
    default:
        break;
    }
}

static void handle_minmax_param(app_menu_ctx_t *ctx, rtrecd_queue_item_t ev)
{
    uint8_t param_count = get_mode_param_count(ctx);
    switch (ev)
    {
    case RTRECD_EVENT_ROTATE_CW:
        list_cw(ctx, param_count);
        break;
    case RTRECD_EVENT_ROTATE_CCW:
        list_ccw(ctx);
        break;
    case RTRECD_EVENT_BUTTON_SHORT:
        ctx->edit_param_index = ctx->cursor;
        if (ctx->edit_mode_index == 4U && ctx->edit_param_index != TT_PARAM_NHIET_DO)
        {
            /* Thoi gian BD, Mo muc to, Mo muc nho: đi thẳng vào edit */
            int32_t vmin, vmax;
            ctx->edit_field_index = 0U;
            get_minmax_range(ctx, &vmin, &vmax);
            ctx->edit_min   = vmin;
            ctx->edit_max   = vmax;
            ctx->edit_value = get_minmax_value(ctx);
            nav_push(ctx, SCREEN_MINMAX_EDIT);
        }
        else if ((ctx->edit_mode_index != 4U) &&
                 (ctx->edit_param_index == (uint8_t)PARAM_TOC_DO_QUAT))
        {
            /* Tốc độ quạt: đi thẳng vào edit (0-100%) */
            int32_t vmin, vmax;
            ctx->edit_field_index = 0U;
            get_minmax_range(ctx, &vmin, &vmax);
            ctx->edit_min   = vmin;
            ctx->edit_max   = vmax;
            ctx->edit_value = get_minmax_value(ctx);
            nav_push(ctx, SCREEN_MINMAX_EDIT);
        }
        else
        {
            /* Nếu vào PARAM_DEN: luôn bắt đầu ở cấp 1 */
            if (ctx->edit_param_index == (uint8_t)PARAM_DEN)
                ctx->den_field_level = 0U;
            /* Nếu vào PARAM_DO_AM: luôn bắt đầu ở cấp 1 */
            if (ctx->edit_param_index == (uint8_t)PARAM_DO_AM)
                ctx->do_am_field_level = 0U;
            nav_push(ctx, SCREEN_MINMAX_FIELD);
        }
        break;
    case RTRECD_EVENT_BUTTON_LONG:
        nav_pop(ctx);
        break;
    default:
        break;
    }
}

static void handle_minmax_field(app_menu_ctx_t *ctx, rtrecd_queue_item_t ev)
{
    /* Độ ẩm: 2 cấp (Su dung / Khong su dung → Min/Max) */
    if (ctx->edit_param_index == (uint8_t)PARAM_DO_AM)
    {
        if (ctx->do_am_field_level == 0U)
        {
            /* Cấp 1: Su dung / Khong su dung */
            switch (ev)
            {
            case RTRECD_EVENT_ROTATE_CW:
                list_cw(ctx, 2U);
                break;
            case RTRECD_EVENT_ROTATE_CCW:
                list_ccw(ctx);
                break;
            case RTRECD_EVENT_BUTTON_SHORT:
            {
                mode_settings_t *cfg = &ctx->mode_cfg[ctx->edit_mode_index];
                switch (ctx->cursor)
                {
                case 0U: /* Su dung: vào cấp 2 chỉnh Min/Max */
                    cfg->do_am_disabled = 0U;
                    ctx->settings_dirty = true;
                    ctx->do_am_field_level = 1U;
                    nav_push(ctx, SCREEN_MINMAX_FIELD);
                    break;
                case 1U: /* Khong su dung: tắt máy tạo ẩm, lưu ngay */
                    cfg->do_am_disabled = 1U;
                    ctx->settings_dirty = true;
                    nav_pop(ctx);
                    break;
                default:
                    break;
                }
                break;
            }
            case RTRECD_EVENT_BUTTON_LONG:
                nav_pop(ctx);
                break;
            default:
                break;
            }
        }
        else
        {
            /* Cấp 2: Min / Max */
            switch (ev)
            {
            case RTRECD_EVENT_ROTATE_CW:
                list_cw(ctx, 2U);
                break;
            case RTRECD_EVENT_ROTATE_CCW:
                list_ccw(ctx);
                break;
            case RTRECD_EVENT_BUTTON_SHORT:
            {
                int32_t vmin, vmax;
                ctx->edit_field_index = ctx->cursor; /* 0=Min, 1=Max */
                get_minmax_range(ctx, &vmin, &vmax);
                ctx->edit_min   = vmin;
                ctx->edit_max   = vmax;
                ctx->edit_value = get_minmax_value(ctx);
                nav_push(ctx, SCREEN_MINMAX_EDIT);
                break;
            }
            case RTRECD_EVENT_BUTTON_LONG:
                ctx->do_am_field_level = 0U;  /* reset về cấp 1 khi pop */
                nav_pop(ctx);
                break;
            default:
                break;
            }
        }
        return;
    }

    /* Đèn có luồng riêng với 2 cấp */
    if (ctx->edit_param_index == (uint8_t)PARAM_DEN)
    {
        if (ctx->den_field_level == 0U)
        {
            /* Cấp 1: Su dung / Khong su dung / 24/24 */
            switch (ev)
            {
            case RTRECD_EVENT_ROTATE_CW:
                list_cw(ctx, 3U);
                break;
            case RTRECD_EVENT_ROTATE_CCW:
                list_ccw(ctx);
                break;
            case RTRECD_EVENT_BUTTON_SHORT:
            {
                mode_settings_t *cfg = &ctx->mode_cfg[ctx->edit_mode_index];
                switch (ctx->cursor)
                {
                case 0U: /* Su dung: vào cấp 2 chỉnh thời gian */
                    cfg->den.den_mode   = (uint8_t)DEN_MODE_SU_DUNG;
                    ctx->settings_dirty = true;
                    ctx->den_field_level = 1U;
                    nav_push(ctx, SCREEN_MINMAX_FIELD);
                    break;
                case 1U: /* Khong su dung: lưu ngay, thoát */
                    cfg->den.den_mode   = (uint8_t)DEN_MODE_KHONG_SU_DUNG;
                    ctx->settings_dirty = true;
                    nav_pop(ctx);
                    break;
                case 2U: /* 24/24: lưu ngay, thoát */
                    cfg->den.den_mode   = (uint8_t)DEN_MODE_24_24;
                    ctx->settings_dirty = true;
                    nav_pop(ctx);
                    break;
                default:
                    break;
                }
                break;
            }
            case RTRECD_EVENT_BUTTON_LONG:
                nav_pop(ctx);
                break;
            default:
                break;
            }
        }
        else
        {
            /* Cấp 2: Time Start / Time Stop */
            switch (ev)
            {
            case RTRECD_EVENT_ROTATE_CW:
                list_cw(ctx, 2U);
                break;
            case RTRECD_EVENT_ROTATE_CCW:
                list_ccw(ctx);
                break;
            case RTRECD_EVENT_BUTTON_SHORT:
            {
                int32_t vmin, vmax;
                ctx->edit_field_index = ctx->cursor; /* 0=Time Start, 1=Time Stop */
                get_minmax_range(ctx, &vmin, &vmax);
                ctx->edit_min   = vmin;
                ctx->edit_max   = vmax;
                ctx->edit_value = get_minmax_value(ctx);
                nav_push(ctx, SCREEN_MINMAX_EDIT);
                break;
            }
            case RTRECD_EVENT_BUTTON_LONG:
                ctx->den_field_level = 0U;  /* reset về cấp 1 khi pop */
                nav_pop(ctx);
                break;
            default:
                break;
            }
        }
        return;
    }

    /* Các thông số khác (Nhiet do, Do am, CO2) */
    switch (ev)
    {
    case RTRECD_EVENT_ROTATE_CW:
        list_cw(ctx, 2U);
        break;
    case RTRECD_EVENT_ROTATE_CCW:
        list_ccw(ctx);
        break;
    case RTRECD_EVENT_BUTTON_SHORT:
    {
        int32_t vmin, vmax;
        ctx->edit_field_index = ctx->cursor;
        get_minmax_range(ctx, &vmin, &vmax);
        ctx->edit_min   = vmin;
        ctx->edit_max   = vmax;
        ctx->edit_value = get_minmax_value(ctx);
        nav_push(ctx, SCREEN_MINMAX_EDIT);
        break;
    }
    case RTRECD_EVENT_BUTTON_LONG:
        nav_pop(ctx);
        break;
    default:
        break;
    }
}

static void handle_minmax_edit(app_menu_ctx_t *ctx, rtrecd_queue_item_t ev)
{
    /* Bước nhảy: CO2 = 100 ppm/bước (chỉ áp dụng ngoài chế độ Thanh trùng), các thông số khác = 1 */
    int32_t step = 1;
    if ((ctx->edit_mode_index != 4U) &&
        ((minmax_param_t)ctx->edit_param_index == PARAM_CO2))
    {
        step = 100;
    }
    switch (ev)
    {
    case RTRECD_EVENT_ROTATE_CW:
        if (ctx->edit_value + step <= ctx->edit_max)
            ctx->edit_value += step;
        else
            ctx->edit_value = ctx->edit_max;
        ctx->dirty = true;
        break;
    case RTRECD_EVENT_ROTATE_CCW:
        if (ctx->edit_value - step >= ctx->edit_min)
            ctx->edit_value -= step;
        else
            ctx->edit_value = ctx->edit_min;
        ctx->dirty = true;
        break;
    case RTRECD_EVENT_BUTTON_SHORT:
        save_minmax_value(ctx);
        nav_pop(ctx); /* xác nhận */
        break;
    case RTRECD_EVENT_BUTTON_LONG:
        nav_pop(ctx); /* huỷ      */
        break;
    default:
        break;
    }
}

static void handle_ds18b20_pos(app_menu_ctx_t *ctx, rtrecd_queue_item_t ev)
{
    switch (ev)
    {
    case RTRECD_EVENT_ROTATE_CW:
        list_cw(ctx, DS18B20_POS_ITEM_COUNT);
        break;
    case RTRECD_EVENT_ROTATE_CCW:
        list_ccw(ctx);
        break;
    case RTRECD_EVENT_BUTTON_SHORT:
        switch (ctx->cursor)
        {
        case 0U: /* So cam bien */
            ctx->edit_value = (int32_t)ctx->ds18b20_target_count;
            ctx->edit_min   = 1;
            ctx->edit_max   = (int32_t)MENU_DS18B20_MAX;
            nav_push(ctx, SCREEN_DS18B20_COUNT);
            break;
        case 1U: /* Hoc vi tri */
            ctx->relearn_req         = 1U;
            ctx->relearn_phase       = DS18B20_LEARN_SEARCHING;
            ctx->relearn_retry_count = 0U;
            ctx->relearn_current_pos = 0U;   /* reset: chưa hỏi vị trí nào */
            ctx->relearn_pos_found   = 0U;
            nav_push(ctx, SCREEN_DS18B20_LEARN);
            break;
        default:
            break;
        }
        break;
    case RTRECD_EVENT_BUTTON_LONG:
        nav_pop(ctx);
        break;
    default:
        break;
    }
}

static void handle_ds18b20_count(app_menu_ctx_t *ctx, rtrecd_queue_item_t ev)
{
    switch (ev)
    {
    case RTRECD_EVENT_ROTATE_CW:
        edit_cw(ctx);
        break;
    case RTRECD_EVENT_ROTATE_CCW:
        edit_ccw(ctx);
        break;
    case RTRECD_EVENT_BUTTON_SHORT:
        /* Lưu số cảm biến và quay lại */
        ctx->ds18b20_target_count = (uint8_t)ctx->edit_value;
        nav_pop(ctx);
        break;
    case RTRECD_EVENT_BUTTON_LONG:
        /* Huỷ, không lưu */
        nav_pop(ctx);
        break;
    default:
        break;
    }
}

static void handle_ds18b20_learn(app_menu_ctx_t *ctx, rtrecd_queue_item_t ev)
{
    switch (ev)
    {
    case RTRECD_EVENT_BUTTON_SHORT:
        /* Chỉ cho thoát khi đã xong hoặc lỗi */
        if (ctx->relearn_phase == DS18B20_LEARN_DONE ||
            ctx->relearn_phase == DS18B20_LEARN_ERROR)
        {
            ctx->relearn_phase = DS18B20_LEARN_IDLE;
            nav_pop(ctx);
        }
        break;
    case RTRECD_EVENT_BUTTON_LONG:
        /* Luôn cho phép thoát bằng nhấn dài */
        ctx->relearn_phase = DS18B20_LEARN_IDLE;
        ctx->relearn_req   = 0U;
        nav_pop(ctx);
        break;
    default:
        break;
    }
}

/* =========================================================================
 * Public API
 * ========================================================================= */

void app_menu_init(app_menu_ctx_t *ctx)
{
    memset(ctx, 0, sizeof(*ctx));

    ctx->screen = SCREEN_WORK1;
    ctx->dirty  = true;

    /* Thời gian mặc định */
    ctx->time_cfg.year   = 2026U;
    ctx->time_cfg.month  = 1U;
    ctx->time_cfg.day    = 1U;
    ctx->time_cfg.hour   = 0U;
    ctx->time_cfg.minute = 0U;
    ctx->time_cfg.second = 0U;

    /* Chế độ mặc định */
    ctx->active_mode = MODE_NGHI;

    /* DS18B20: số cảm biến mặc định */
    ctx->ds18b20_target_count = 4U;

    /* MinMax mặc định cho cả 5 chế độ */
    for (uint8_t i = 0U; i < 5U; i++)
    {
        ctx->mode_cfg[i].nhiet_do.min = 20;
        ctx->mode_cfg[i].nhiet_do.max = 35;
        ctx->mode_cfg[i].do_am.min    = 50;
        ctx->mode_cfg[i].do_am.max    = 95;
        ctx->mode_cfg[i].co2.min      = 400;
        ctx->mode_cfg[i].co2.max      = 2000;
        ctx->mode_cfg[i].den.time_start_h = 6U;
        ctx->mode_cfg[i].den.time_start_m = 0U;
        ctx->mode_cfg[i].den.time_stop_h  = 20U;
        ctx->mode_cfg[i].den.time_stop_m  = 0U;
        ctx->mode_cfg[i].den.den_mode     = (uint8_t)DEN_MODE_KHONG_SU_DUNG;
        ctx->mode_cfg[i].do_am_disabled   = 0U;   /* mặc định: dùng máy tạo ẩm */
        ctx->mode_cfg[i].toc_do_quat      = MODE_DEFAULT_FAN_PERCENT;

        /* Thanh trùng defaults */
        ctx->mode_cfg[i].thanh_trung_initial_minutes = 10U;
        ctx->mode_cfg[i].sg90_mo_to_deg  = 180U;
        ctx->mode_cfg[i].sg90_mo_nho_deg = 0U;
    }

    /* --- [0] Chay to: T 32-35, A 70-80, CO2 4000-5000, den khong su dung --- */
    ctx->mode_cfg[0].nhiet_do.min = 32;
    ctx->mode_cfg[0].nhiet_do.max = 35;
    ctx->mode_cfg[0].do_am.min    = 70;
    ctx->mode_cfg[0].do_am.max    = 80;
    ctx->mode_cfg[0].co2.min      = 4000;
    ctx->mode_cfg[0].co2.max      = 5000;
    ctx->mode_cfg[0].den.den_mode = (uint8_t)DEN_MODE_KHONG_SU_DUNG;
    ctx->mode_cfg[0].toc_do_quat  = MODE_DEFAULT_FAN_PERCENT;

    /* --- [1] Kich Dinh ghim: T 28-32, A 84-94, CO2 700-1200, den 24/24 --- */
    ctx->mode_cfg[1].nhiet_do.min = 28;
    ctx->mode_cfg[1].nhiet_do.max = 32;
    ctx->mode_cfg[1].do_am.min    = 84;
    ctx->mode_cfg[1].do_am.max    = 94;
    ctx->mode_cfg[1].co2.min      = 700;
    ctx->mode_cfg[1].co2.max      = 1200;
    ctx->mode_cfg[1].den.den_mode = (uint8_t)DEN_MODE_24_24;
    ctx->mode_cfg[1].toc_do_quat  = MODE_DEFAULT_FAN_PERCENT;

    /* --- [2] Dinh ghim: T 28-32, A 84-94, CO2 700-1200, den khong su dung --- */
    ctx->mode_cfg[2].nhiet_do.min = 28;
    ctx->mode_cfg[2].nhiet_do.max = 32;
    ctx->mode_cfg[2].do_am.min    = 84;
    ctx->mode_cfg[2].do_am.max    = 94;
    ctx->mode_cfg[2].co2.min      = 700;
    ctx->mode_cfg[2].co2.max      = 1200;
    ctx->mode_cfg[2].den.den_mode = (uint8_t)DEN_MODE_KHONG_SU_DUNG;
    ctx->mode_cfg[2].toc_do_quat  = MODE_DEFAULT_FAN_PERCENT;

    /* --- [3] Qua the: T 28-32, CO2 700-1200, do_am khong su dung, den khong su dung --- */
    ctx->mode_cfg[3].nhiet_do.min    = 28;
    ctx->mode_cfg[3].nhiet_do.max    = 32;
    ctx->mode_cfg[3].co2.min         = 700;
    ctx->mode_cfg[3].co2.max         = 1200;
    ctx->mode_cfg[3].do_am_disabled  = 1U;   /* Qua the: không dùng máy tạo ẩm */
    ctx->mode_cfg[3].den.den_mode    = (uint8_t)DEN_MODE_KHONG_SU_DUNG;
    ctx->mode_cfg[3].toc_do_quat     = MODE_DEFAULT_FAN_PERCENT;

    /* --- [4] Thanh trung: T 70-75, BD 30 phut, Mo to 170 deg, Mo nho 10 deg --- */
    ctx->mode_cfg[4].nhiet_do.min = 70;
    ctx->mode_cfg[4].nhiet_do.max = 75;
    ctx->mode_cfg[4].thanh_trung_initial_minutes = 30U;
    ctx->mode_cfg[4].sg90_mo_to_deg  = 170U;
    ctx->mode_cfg[4].sg90_mo_nho_deg = 10U;

    /*
     * Load từ Flash (đè lên giá trị mặc định ở trên nếu Flash hợp lệ).
     * Nếu Flash chưa có dữ liệu, giữ nguyên giá trị mặc định.
     */
    app_settings_load(ctx);
}

void app_menu_handle_event(app_menu_ctx_t *ctx, rtrecd_queue_item_t ev)
{
    switch (ctx->screen)
    {
    case SCREEN_WORK1:        handle_work1(ctx, ev);        break;
    case SCREEN_WORK2:        handle_work2(ctx, ev);        break;
    case SCREEN_WORK3:        handle_work3(ctx, ev);        break;
    case SCREEN_MAIN_MENU:    handle_main_menu(ctx, ev);    break;
    case SCREEN_MODE_SELECT:  handle_mode_select(ctx, ev);  break;
    case SCREEN_TIME_MENU:    handle_time_menu(ctx, ev);    break;
    case SCREEN_TIME_EDIT:    handle_time_edit(ctx, ev);    break;
    case SCREEN_MINMAX_MODE:  handle_minmax_mode(ctx, ev);  break;
    case SCREEN_MINMAX_PARAM: handle_minmax_param(ctx, ev); break;
    case SCREEN_MINMAX_FIELD: handle_minmax_field(ctx, ev); break;
    case SCREEN_MINMAX_EDIT:  handle_minmax_edit(ctx, ev);  break;
    case SCREEN_DS18B20_POS:    handle_ds18b20_pos(ctx, ev);    break;
    case SCREEN_DS18B20_COUNT:  handle_ds18b20_count(ctx, ev); break;
    case SCREEN_DS18B20_LEARN:  handle_ds18b20_learn(ctx, ev); break;
    default: break;
    }
}

void app_menu_render(app_menu_ctx_t *ctx)
{
    /*
     * Màn hình học vị trí: luôn vẽ lại khi đang tìm kiếm (không dùng dirty)
     * để TaskLCD liên tục cập nhật trạng thái từ TaskDS18B20.
     */
    if (ctx->screen == SCREEN_DS18B20_LEARN)
    {
        render_ds18b20_learn(ctx);
        return;
    }

    if (!ctx->dirty) return;
    ctx->dirty = false;

    switch (ctx->screen)
    {
    case SCREEN_WORK1:        render_work1(ctx);        break;
    case SCREEN_WORK2:        render_work2(ctx);        break;
    case SCREEN_WORK3:        render_work3(ctx);        break;
    case SCREEN_MAIN_MENU:    render_main_menu(ctx);    break;
    case SCREEN_MODE_SELECT:  render_mode_select(ctx);  break;
    case SCREEN_TIME_MENU:    render_time_menu(ctx);    break;
    case SCREEN_TIME_EDIT:    render_time_edit(ctx);    break;
    case SCREEN_MINMAX_MODE:  render_minmax_mode(ctx);  break;
    case SCREEN_MINMAX_PARAM: render_minmax_param(ctx); break;
    case SCREEN_MINMAX_FIELD: render_minmax_field(ctx); break;
    case SCREEN_MINMAX_EDIT:  render_minmax_edit(ctx);  break;
    case SCREEN_DS18B20_POS:   render_ds18b20_pos(ctx);   break;
    case SCREEN_DS18B20_COUNT: render_ds18b20_count(ctx); break;
    case SCREEN_DS18B20_LEARN: render_ds18b20_learn(ctx); break;
    default: break;
    }
}

void app_menu_update_scd41(app_menu_ctx_t *ctx, const scd41_queue_item_t *data)
{
    ctx->scd41 = *data;
    if (ctx->screen == SCREEN_WORK1 || ctx->screen == SCREEN_WORK2
        || ctx->screen == SCREEN_WORK3)
    {
        ctx->dirty = true;
    }
}

void app_menu_set_scd41_fault(app_menu_ctx_t *ctx, bool fault)
{
    if (ctx->scd41_fault == fault) return;
    ctx->scd41_fault = fault;
    if (ctx->screen == SCREEN_WORK1 || ctx->screen == SCREEN_WORK2
        || ctx->screen == SCREEN_WORK3)
    {
        ctx->dirty = true;
    }
}

void app_menu_update_ds18b20(app_menu_ctx_t *ctx, const Ds18b20QueueItem *data)
{
    /* sensorIndex từ library là 1-based (1..maxDevices).
     * Chuyển sang 0-based để lưu vào mảng ds18b20[0..MENU_DS18B20_MAX-1]. */
    if (data->sensorIndex == 0U) return;  /* không hợp lệ */
    uint8_t idx = (uint8_t)(data->sensorIndex - 1U);
    if (idx >= MENU_DS18B20_MAX) return;

    ctx->ds18b20[idx] = *data;
    /* Sensor gửi data tốt → xóa fault bit tương ứng */
    ctx->ds18b20_fault_mask &= (uint8_t)~(uint8_t)(1U << idx);
    if ((uint8_t)(idx + 1U) > ctx->ds18b20_count)
    {
        ctx->ds18b20_count = (uint8_t)(idx + 1U);
    }

    if (ctx->screen == SCREEN_WORK1 || ctx->screen == SCREEN_WORK2
        || ctx->screen == SCREEN_WORK3)
    {
        ctx->dirty = true;
    }
}

void app_menu_mark_dirty(app_menu_ctx_t *ctx)
{
    ctx->dirty = true;
}

/* =========================================================================
 * RTC integration
 * ========================================================================= */

void app_menu_update_time_from_rtc(app_menu_ctx_t *ctx, RTC_HandleTypeDef *hrtc)
{
    RTC_TimeTypeDef sTime = {0};
    RTC_DateTypeDef sDate = {0};

    /*
     * Lưu ý STM32F1 legacy RTC:
     * Phải gọi GetTime trước GetDate để latch đúng giá trị.
     */
    if (HAL_RTC_GetTime(hrtc, &sTime, RTC_FORMAT_BIN) != HAL_OK) return;
    if (HAL_RTC_GetDate(hrtc, &sDate, RTC_FORMAT_BIN) != HAL_OK) return;

    ctx->time_cfg.hour   = sTime.Hours;
    ctx->time_cfg.minute = sTime.Minutes;
    ctx->time_cfg.second = sTime.Seconds;
    ctx->time_cfg.day    = sDate.Date;
    ctx->time_cfg.month  = sDate.Month;
    ctx->time_cfg.year   = 2000U + (uint16_t)sDate.Year;

    /* Vẽ lại nếu đang ở màn hình làm việc */
    if (ctx->screen == SCREEN_WORK1 || ctx->screen == SCREEN_WORK2
        || ctx->screen == SCREEN_WORK3)
    {
        ctx->dirty = true;
    }
}

void app_menu_write_time_to_rtc(app_menu_ctx_t *ctx, RTC_HandleTypeDef *hrtc)
{
    RTC_TimeTypeDef sTime = {0};
    RTC_DateTypeDef sDate = {0};

    sTime.Hours   = ctx->time_cfg.hour;
    sTime.Minutes = ctx->time_cfg.minute;
    sTime.Seconds = ctx->time_cfg.second;

    sDate.Date    = ctx->time_cfg.day;
    sDate.Month   = ctx->time_cfg.month;
    sDate.Year    = (uint8_t)(ctx->time_cfg.year % 100U);
    sDate.WeekDay = RTC_WEEKDAY_MONDAY; /* Ngày trong tuần có thể bỏ qua */

    HAL_RTC_SetTime(hrtc, &sTime, RTC_FORMAT_BIN);
    HAL_RTC_SetDate(hrtc, &sDate, RTC_FORMAT_BIN);

    /* Ghi magic number vào BKP DR1 để giữ thời gian sau reset */
    HAL_RTCEx_BKUPWrite(hrtc, RTC_BKP_DR1, 0xA5A5U);

    /*
     * Lưu ngày vào BKP DR2/DR3 riêng để phòng trường hợp debugger
     * xoá BKP registers của HAL nhưng không xoá DR1.
     * DR2 [15:9] = year-2000  [8:5] = month  [4:0] = day
     * DR3 = magic 0x5A5A
     */
    uint16_t date_bkp = (uint16_t)(((ctx->time_cfg.year % 100U) << 9) |
                                   ((uint16_t)ctx->time_cfg.month << 5) |
                                    (uint16_t)ctx->time_cfg.day);
    HAL_RTCEx_BKUPWrite(hrtc, RTC_BKP_DR2, date_bkp);
    HAL_RTCEx_BKUPWrite(hrtc, RTC_BKP_DR3, 0x5A5AU);

    ctx->time_rtc_dirty = false;
}
