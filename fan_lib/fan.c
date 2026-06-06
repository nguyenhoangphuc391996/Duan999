/*
 * fan.c
 *
 * Thư viện điều khiển quạt – toàn bộ logic:
 *   1. Học tốc độ RPM (coast-stop → sweep 0%→100%)
 *   2. Giám sát RPM so với profile đã học (cứ 5 s/lần, cần 2 lần vượt ngưỡng)
 *   3. Giám sát an toàn cơ học (<100 RPM liên tục 2 s → tắt ngay)
 *   4. Phục hồi lỗi:
 *      - Lỗi cơ học: RPM quay lên >= 100 RPM → mở lại
 *      - Lỗi lệch học: chờ dừng hẳn → quay tay >= 100 RPM → mở lại
 *
 * ITM debug (enable bằng itm_set_library_enabled(ITM_LIB_FAN, true)):
 *   [FAN] ...  – mọi sự kiện quan trọng được in ra kênh ITM_LIB_FAN.
 */

#include "fan.h"
#include "app_menu.h"
#include "app_settings.h"
#include "itm.h"
#include <string.h>

/* =========================================================================
 * Macro ITM
 * ========================================================================= */

#define FAN_LOG(...) itm_printf_library(ITM_LIB_FAN, "[FAN] " __VA_ARGS__)

/* =========================================================================
 * Kiểu nội bộ – trạng thái phục hồi lỗi lệch học
 * ========================================================================= */

typedef enum
{
    FAN_LEARN_REC_IDLE = 0,
    FAN_LEARN_REC_COASTING,  /**< Chờ quạt dừng hẳn sau khi cắt PWM */
    FAN_LEARN_REC_WAIT_SPIN, /**< Đã dừng, chờ quay tay >= 100 RPM  */
} fan_learn_rec_phase_t;

static fan_learn_rec_phase_t s_learn_rec_phase = FAN_LEARN_REC_IDLE;
static uint8_t               s_coast_stop_sec  = 0U;

/* =========================================================================
 * Helper: API công khai
 * ========================================================================= */

bool fan_has_any_fault_locked(const fan_state_t *state)
{
    return ((state->alarm_active != 0U) || (state->low_speed_fault != 0U));
}

/* =========================================================================
 * Helpers nội bộ – display, phục hồi
 * ========================================================================= */

/** Đánh dấu dirty WORK1 nếu đang ở màn hình đó (caller giữ mutex). */
static void s_mark_work1_dirty(app_menu_ctx_t *menu)
{
    if (menu->screen == SCREEN_WORK1)
    {
        menu->dirty = true;
    }
}

static void s_learn_rec_reset(void)
{
    s_learn_rec_phase = FAN_LEARN_REC_IDLE;
    s_coast_stop_sec  = 0U;
}

static void s_learn_rec_on_fault(void)
{
    s_learn_rec_phase = FAN_LEARN_REC_COASTING;
    s_coast_stop_sec  = 0U;
}

/* =========================================================================
 * Quản lý fault
 * ========================================================================= */

/**
 * @brief Kích hoạt lỗi cơ học (kẹt / chạy chậm): tắt PWM ngay.
 */
static void s_enter_low_speed_fault(fan_ctx_t *fc, uint16_t rpm, uint8_t duty)
{
    bool newly_fault = false;

    osMutexAcquire(fc->mutex, osWaitForever);
    fc->menu_ctx->fan.low_speed_fault = 1U;
    if (fc->menu_ctx->fan.force_off == 0U)
    {
        fc->menu_ctx->fan.force_off = 1U;
        newly_fault = true;
    }
    s_mark_work1_dirty(fc->menu_ctx);
    osMutexRelease(fc->mutex);

    FAN_LOG("LOW SPEED FAULT! duty=%u%% rpm=%u (need>=%u)\r\n",
            (unsigned)duty, (unsigned)rpm, (unsigned)FAN_FAULT_RPM_MIN);

    if (newly_fault)
    {
        output_fan_set_percent(fc->output, 0U);
    }
}

/**
 * @brief Kích hoạt lỗi lệch RPM profile học: tắt PWM + báo QER.
 */
static void s_enter_learn_fault(fan_ctx_t *fc,
                                uint16_t   current_rpm,
                                uint16_t   learned_rpm_val,
                                uint8_t    duty)
{
    bool newly_fault = false;

    osMutexAcquire(fc->mutex, osWaitForever);
    fc->menu_ctx->fan.alarm_active = 1U;
    if (fc->menu_ctx->fan.force_off == 0U)
    {
        fc->menu_ctx->fan.force_off = 1U;
        newly_fault = true;
    }
    s_mark_work1_dirty(fc->menu_ctx);
    osMutexRelease(fc->mutex);

    s_learn_rec_on_fault();

    FAN_LOG("LEARN FAULT! duty=%u%% current=%u RPM learned=%u RPM (tol=+/-%u%%)\r\n",
            (unsigned)duty, (unsigned)current_rpm,
            (unsigned)learned_rpm_val, (unsigned)FAN_LEARN_TOLERANCE_PERCENT);

    if (newly_fault)
    {
        output_fan_set_percent(fc->output, 0U);
    }
}

/**
 * @brief Phục hồi lỗi cơ học: bỏ force_off, giữ alarm_active nếu còn lệch học.
 */
static void s_recovery_low_speed(fan_ctx_t *fc)
{
    osMutexAcquire(fc->mutex, osWaitForever);
    fc->menu_ctx->fan.low_speed_fault = 0U;
    fc->menu_ctx->fan.force_off       = 0U;
    s_mark_work1_dirty(fc->menu_ctx);
    osMutexRelease(fc->mutex);

    FAN_LOG("Recovery: low-speed fault cleared, fan allowed to run\r\n");
}

/**
 * @brief Phục hồi lỗi lệch học (sau khi quay tay xác nhận): xóa alarm + force_off.
 */
static void s_recovery_learn_alarm(fan_ctx_t *fc)
{
    osMutexAcquire(fc->mutex, osWaitForever);
    fc->menu_ctx->fan.alarm_active = 0U;
    fc->menu_ctx->fan.force_off    = 0U;
    s_mark_work1_dirty(fc->menu_ctx);
    osMutexRelease(fc->mutex);

    s_learn_rec_reset();
    FAN_LOG("Recovery: learn alarm cleared (manual spin confirmed)\r\n");
}

/* =========================================================================
 * Đo RPM
 * ========================================================================= */

static bool s_tach_stopped(uint32_t pps)
{
    return (pps <= (uint32_t)FAN_COAST_STOP_MAX_PPS);
}

static uint32_t s_measure_pulses_1s(fan_ctx_t *fc)
{
    uint32_t start = *fc->tach_count;
    osDelay(1000U);
    return (*fc->tach_count - start);
}

static uint16_t s_measure_rpm_1s(fan_ctx_t *fc)
{
    return FAN_PULSES_TO_RPM(s_measure_pulses_1s(fc));
}

static uint16_t s_measure_rpm_avg(fan_ctx_t *fc, uint8_t samples)
{
    if (samples == 0U) { samples = 1U; }
    uint32_t sum = 0U;
    for (uint8_t i = 0U; i < samples; i++)
    {
        sum += (uint32_t)s_measure_rpm_1s(fc);
    }
    return (uint16_t)(sum / (uint32_t)samples);
}

/* =========================================================================
 * Profile học
 * ========================================================================= */

/** Nội suy RPM kỳ vọng tại duty_pct từ bảng học 11 bước (0%-100%). */
static uint16_t s_expected_rpm(const uint16_t *learned, uint8_t duty_pct)
{
    if (duty_pct >= 100U) { return learned[FAN_LEARN_STEPS - 1U]; }

    uint8_t lo  = duty_pct / 10U;
    uint8_t hi  = lo + 1U;
    if (hi >= FAN_LEARN_STEPS) { hi = FAN_LEARN_STEPS - 1U; }

    uint16_t r_lo = learned[lo];
    uint16_t r_hi = learned[hi];
    uint8_t  frac = duty_pct - (uint8_t)(lo * 10U);

    return (uint16_t)(r_lo +
           (((uint32_t)r_hi - (uint32_t)r_lo) * (uint32_t)frac) / 10U);
}

/** true nếu profile học cho kết quả RPM hợp lý tại duty_pct. */
static bool s_profile_valid(const uint16_t *learned, uint8_t duty_pct)
{
    if (duty_pct < FAN_MONITOR_MIN_PWM_PERCENT) { return true; }
    return (s_expected_rpm(learned, duty_pct) >= FAN_FAULT_RPM_MIN);
}

/** true nếu current_rpm nằm trong ±pct% của learned_rpm (có sàn tuyệt đối). */
static bool s_rpm_within_tolerance(uint16_t current_rpm, uint16_t learned_rpm_val,
                                   uint8_t pct)
{
    if (learned_rpm_val == 0U) { return true; }

    uint32_t diff = (current_rpm > learned_rpm_val) ?
                    ((uint32_t)current_rpm - (uint32_t)learned_rpm_val) :
                    ((uint32_t)learned_rpm_val - (uint32_t)current_rpm);

    uint32_t tol = ((uint32_t)learned_rpm_val * (uint32_t)pct) / 100U;
    if (tol < (uint32_t)FAN_LEARN_TOLERANCE_RPM_MIN)
    {
        tol = (uint32_t)FAN_LEARN_TOLERANCE_RPM_MIN;
    }
    return (diff <= tol);
}

/* =========================================================================
 * Chờ quạt dừng hẳn (trước khi bắt đầu học)
 * ========================================================================= */

/**
 * @brief Tắt PWM và chờ quạt dừng hẳn (hết quán tính).
 * @return true nếu xác nhận đã dừng (hoặc timeout), false nếu user hủy.
 */
static bool s_wait_coast_stop(fan_ctx_t *fc)
{
    uint8_t  stop_sec   = 0U;
    uint32_t wait_start = osKernelGetTickCount();

    /* Tắt ngay lập tức (bypass output_ctrl vì đang trong quá trình learn) */
    output_fan_set_percent(fc->output, 0U);

    osMutexAcquire(fc->mutex, osWaitForever);
    fc->menu_ctx->fan.learn_pwm_pct = 0U;
    fc->menu_ctx->fan.learn_phase   = FAN_LEARN_WAIT_STOP;
    s_mark_work1_dirty(fc->menu_ctx);
    osMutexRelease(fc->mutex);

    FAN_LOG("Waiting for fan to coast to stop...\r\n");

    while (stop_sec < FAN_COAST_STOP_CONFIRM_SEC)
    {
        /* Kiểm tra hủy từ menu (nhấn dài) */
        if (fc->menu_ctx->fan.learn_active == 0U)
        {
            FAN_LOG("Learn cancelled during coast-stop\r\n");
            return false;
        }

        uint32_t pps = s_measure_pulses_1s(fc);
        FAN_LOG("Coast: %lu pps (stop when <=%u for %u s)\r\n",
                (unsigned long)pps,
                (unsigned)FAN_COAST_STOP_MAX_PPS,
                (unsigned)FAN_COAST_STOP_CONFIRM_SEC);

        if (s_tach_stopped(pps))
        {
            stop_sec++;
        }
        else
        {
            stop_sec = 0U;
        }

        if ((osKernelGetTickCount() - wait_start) >= FAN_COAST_STOP_TIMEOUT_MS)
        {
            FAN_LOG("Coast-stop timeout (%u ms) – starting learn anyway\r\n",
                    (unsigned)FAN_COAST_STOP_TIMEOUT_MS);
            return true;
        }
    }

    FAN_LOG("Fan confirmed stopped\r\n");
    return true;
}

/* =========================================================================
 * Giám sát an toàn (gọi mỗi ~1 s)
 * ========================================================================= */

static void s_safety_monitor(fan_ctx_t *fc, uint32_t now_tick)
{
    static uint32_t s_last_tick  = 0U;
    static uint32_t s_tach_last  = 0U;
    static uint8_t  s_last_duty  = 0xFFU;
    static uint8_t  s_slow_sec   = 0U;
    static uint32_t s_grace_until = 0U;
    static bool     s_inited     = false;

    if (!s_inited)
    {
        s_tach_last = *fc->tach_count;
        s_inited    = true;
    }

    if ((now_tick - s_last_tick) < 1000U) { return; }
    s_last_tick = now_tick;

    /* Đọc trạng thái hiện tại (mutex) */
    uint8_t    learn_active = 0U;
    uint8_t    force_off    = 0U;
    uint8_t    alarm_active = 0U;
    uint8_t    low_spd      = 0U;
    uint8_t    duty         = 0U;
    app_mode_t mode         = MODE_NGHI;

    osMutexAcquire(fc->mutex, osWaitForever);
    learn_active = fc->menu_ctx->fan.learn_active;
    force_off    = fc->menu_ctx->fan.force_off;
    alarm_active = fc->menu_ctx->fan.alarm_active;
    low_spd      = fc->menu_ctx->fan.low_speed_fault;
    mode         = fc->menu_ctx->active_mode;
    if (learn_active != 0U)
    {
        duty = fc->menu_ctx->fan.learn_pwm_pct;
    }
    else if (mode <= MODE_QUA_THE)
    {
        duty = fc->menu_ctx->mode_cfg[mode].toc_do_quat;
    }
    osMutexRelease(fc->mutex);

    /* Đếm xung trong 1 s */
    uint32_t tach_now = *fc->tach_count;
    uint32_t pulses   = tach_now - s_tach_last;
    s_tach_last       = tach_now;
    uint16_t rpm      = FAN_PULSES_TO_RPM(pulses);

    /* ---- Xử lý phục hồi khi force_off đang kích hoạt ---- */
    if (force_off != 0U)
    {
        if ((alarm_active != 0U) && (low_spd == 0U))
        {
            /* Lỗi lệch học: chờ hết quán tính, rồi quay tay >= 100 RPM */
            switch (s_learn_rec_phase)
            {
            case FAN_LEARN_REC_COASTING:
                if (s_tach_stopped(pulses))
                {
                    if (s_coast_stop_sec < 255U) { s_coast_stop_sec++; }
                    if (s_coast_stop_sec >= FAN_COAST_STOP_CONFIRM_SEC)
                    {
                        s_learn_rec_phase = FAN_LEARN_REC_WAIT_SPIN;
                        s_coast_stop_sec  = 0U;
                        FAN_LOG("Recovery: coasted to stop – please spin fan by hand\r\n");
                    }
                }
                else
                {
                    s_coast_stop_sec = 0U;
                }
                break;

            case FAN_LEARN_REC_WAIT_SPIN:
                FAN_LOG("Recovery spin: rpm=%u (need>=%u)\r\n",
                        (unsigned)rpm, (unsigned)FAN_RECOVERY_RPM_MIN);
                if (rpm >= (uint16_t)FAN_RECOVERY_RPM_MIN)
                {
                    s_recovery_learn_alarm(fc);
                    s_slow_sec = 0U;
                }
                break;

            default:
                s_learn_rec_on_fault();
                break;
            }
        }
        else
        {
            /* Lỗi cơ học: RPM quay lên >= 100 RPM → mở lại */
            FAN_LOG("Recovery wait (mech): rpm=%u (need>=%u)\r\n",
                    (unsigned)rpm, (unsigned)FAN_RECOVERY_RPM_MIN);
            if (rpm >= (uint16_t)FAN_RECOVERY_RPM_MIN)
            {
                s_recovery_low_speed(fc);
                s_slow_sec = 0U;
            }
        }
        return;
    }

    /* Không giám sát khi đang học, hoặc chế độ không có quạt */
    if ((learn_active != 0U) || (mode == MODE_NGHI) || (mode == MODE_THANH_TRUNG))
    {
        s_slow_sec  = 0U;
        s_last_duty = duty;
        return;
    }

    /* Đổi duty → reset bộ đếm + grace period */
    if (duty != s_last_duty)
    {
        s_last_duty   = duty;
        s_slow_sec    = 0U;
        s_grace_until = now_tick + FAN_STARTUP_GRACE_MS;
        return;
    }

    /* Dưới ngưỡng PWM tối thiểu → không giám sát RPM */
    if (duty < FAN_MONITOR_MIN_PWM_PERCENT)
    {
        s_slow_sec = 0U;
        return;
    }

    /* Đang trong grace period khởi động */
    if (now_tick < s_grace_until) { return; }

    /* RPM đủ cao → không lỗi */
    if (rpm >= (uint16_t)FAN_FAULT_RPM_MIN)
    {
        s_slow_sec = 0U;
        return;
    }

    /* RPM thấp: đếm giây */
    if (s_slow_sec < 255U) { s_slow_sec++; }

    FAN_LOG("Low RPM: %u (duty=%u%%, count=%u/%u)\r\n",
            (unsigned)rpm, (unsigned)duty,
            (unsigned)s_slow_sec, (unsigned)FAN_LOW_SPEED_FAULT_SEC);

    if (s_slow_sec >= FAN_LOW_SPEED_FAULT_SEC)
    {
        s_enter_low_speed_fault(fc, rpm, duty);
        s_slow_sec = 0U;
    }
}

/* =========================================================================
 * Trình tự học
 * ========================================================================= */

static void s_run_learn_sequence(fan_ctx_t *fc)
{
    uint8_t step = 0U;

    /* Xóa mọi lỗi cũ trước khi học */
    osMutexAcquire(fc->mutex, osWaitForever);
    fc->menu_ctx->fan.alarm_active    = 0U;
    fc->menu_ctx->fan.low_speed_fault = 0U;
    fc->menu_ctx->fan.force_off       = 0U;
    s_mark_work1_dirty(fc->menu_ctx);
    osMutexRelease(fc->mutex);

    s_learn_rec_reset();
    FAN_LOG("=== Fan learn started ===\r\n");

    /* Tắt quạt + chờ dừng hẳn */
    if (!s_wait_coast_stop(fc))
    {
        /* User hủy */
        osMutexAcquire(fc->mutex, osWaitForever);
        fc->menu_ctx->fan.learn_active  = 0U;
        fc->menu_ctx->fan.learn_pwm_pct = 0U;
        fc->menu_ctx->fan.learn_phase   = FAN_LEARN_IDLE;
        osMutexRelease(fc->mutex);
        FAN_LOG("Fan learn cancelled before start\r\n");
        return;
    }

    /* Bắt đầu sweep PWM */
    osMutexAcquire(fc->mutex, osWaitForever);
    fc->menu_ctx->fan.learn_phase = FAN_LEARN_RUNNING;
    s_mark_work1_dirty(fc->menu_ctx);
    osMutexRelease(fc->mutex);

    for (step = 0U; step < FAN_LEARN_STEPS; step++)
    {
        if (fc->menu_ctx->fan.learn_active == 0U) { break; } /* user hủy */

        uint8_t duty = (uint8_t)(step * 10U);

        osMutexAcquire(fc->mutex, osWaitForever);
        fc->menu_ctx->fan.learn_step    = step;
        fc->menu_ctx->fan.learn_pwm_pct = duty;
        osMutexRelease(fc->mutex);

        /*
         * output_ctrl (TaskOutput, cứ 200 ms) sẽ đọc learn_pwm_pct và đặt PWM.
         * Chờ ổn định trước khi đo RPM:
         *   step 0 (0%):   500 ms – xác nhận quạt đứng yên
         *   10-20%:       4000 ms – vùng khởi động khó đoán
         *   30-40%:       3000 ms
         *   50-100%:      2000 ms
         */
        if (step == 0U)        { osDelay(500U);  }
        else if (duty <= 20U)  { osDelay(4000U); }
        else if (duty <= 40U)  { osDelay(3000U); }
        else                   { osDelay(2000U); }

        if (fc->menu_ctx->fan.learn_active == 0U) { break; }

        /* Đo RPM trong 1 s */
        uint32_t cnt_start = *fc->tach_count;
        osDelay(1000U);
        uint32_t pulses = *fc->tach_count - cnt_start;
        uint16_t rpm    = FAN_PULSES_TO_RPM(pulses);

        osMutexAcquire(fc->mutex, osWaitForever);
        fc->menu_ctx->fan.learned_rpm[step] = rpm;
        osMutexRelease(fc->mutex);

        FAN_LOG("Step %2u (%3u%%): %4u RPM\r\n",
                (unsigned)step, (unsigned)duty, (unsigned)rpm);
    }

    /* ---- Kết thúc học ---- */
    osMutexAcquire(fc->mutex, osWaitForever);
    fc->menu_ctx->fan.learn_active  = 0U;
    fc->menu_ctx->fan.learn_pwm_pct = 0U;

    if (step == FAN_LEARN_STEPS)
    {
        fc->menu_ctx->fan.learn_done  = 1U;
        fc->menu_ctx->fan.learn_phase = FAN_LEARN_DONE;
        osMutexRelease(fc->mutex);

        /* Lưu Flash bên ngoài mutex – thao tác dài (~ms) */
        app_settings_save(fc->menu_ctx);
        FAN_LOG("=== Fan learn DONE – profile saved to Flash ===\r\n");
    }
    else
    {
        fc->menu_ctx->fan.learn_phase = FAN_LEARN_IDLE;
        osMutexRelease(fc->mutex);
        FAN_LOG("=== Fan learn ABORTED at step %u ===\r\n", (unsigned)step);
    }
}

/* =========================================================================
 * Public API
 * ========================================================================= */

void fan_ctx_init(fan_ctx_t        *fc,
                  app_menu_ctx_t   *menu_ctx,
                  output_t         *output,
                  volatile uint32_t *tach_count,
                  osMutexId_t       mutex,
                  GPIO_TypeDef     *alarm_port,
                  uint16_t          alarm_pin)
{
    if (fc == NULL) { return; }
    fc->menu_ctx   = menu_ctx;
    fc->output     = output;
    fc->tach_count = tach_count;
    fc->mutex      = mutex;
    fc->alarm_port = alarm_port;
    fc->alarm_pin  = alarm_pin;
}

void fan_task_body(fan_ctx_t *fc)
{
    static uint32_t s_monitor_tick   = 0U;
    static uint8_t  s_mon_last_duty  = 0xFFU;
    static app_mode_t s_mon_last_mode = MODE_COUNT;
    static uint32_t s_mon_stable_tick = 0U;
    static uint8_t  s_fault_hits     = 0U;
    static bool     s_first_call     = true;

    if (s_first_call)
    {
        s_monitor_tick    = osKernelGetTickCount();
        s_mon_stable_tick = s_monitor_tick;
        s_first_call      = false;
    }

    /* ================================================================
     * KHỐI HỌC TỐC ĐỘ QUẠT
     * ============================================================= */
    if (fc->menu_ctx->fan.learn_req ||
        (fc->menu_ctx->fan.learn_active &&
         (fc->menu_ctx->fan.learn_phase == FAN_LEARN_WAIT_STOP)))
    {
        /* Nhận yêu cầu học */
        if (fc->menu_ctx->fan.learn_req)
        {
            fc->menu_ctx->fan.learn_req = 0U;
        }
        fc->menu_ctx->fan.learn_active = 1U;
        fc->menu_ctx->fan.learn_done   = 0U;

        s_mon_stable_tick = osKernelGetTickCount();
        s_fault_hits      = 0U;

        s_run_learn_sequence(fc);

        /* Reset bộ đếm giám sát sau khi học xong */
        s_mon_stable_tick = osKernelGetTickCount();
        s_fault_hits      = 0U;
        s_monitor_tick    = osKernelGetTickCount();
    }

    /* ================================================================
     * KHỐI GIÁM SÁT RPM ĐÃ HỌC (5 s / lần)
     * ============================================================= */
    if ((osKernelGetTickCount() - s_monitor_tick) >= 5000U)
    {
        uint8_t    fan_done    = 0U;
        uint8_t    fan_active  = 0U;
        uint8_t    force_off   = 0U;
        app_mode_t active_mode = MODE_NGHI;
        uint8_t    cur_duty    = 0U;
        uint16_t   learned[FAN_LEARN_STEPS];

        osMutexAcquire(fc->mutex, osWaitForever);
        fan_done    = fc->menu_ctx->fan.learn_done;
        fan_active  = fc->menu_ctx->fan.learn_active;
        force_off   = fc->menu_ctx->fan.force_off;
        active_mode = fc->menu_ctx->active_mode;
        if ((active_mode <= MODE_QUA_THE) && (fan_active == 0U))
        {
            cur_duty = fc->menu_ctx->mode_cfg[active_mode].toc_do_quat;
        }
        memcpy(learned, fc->menu_ctx->fan.learned_rpm, sizeof(learned));
        osMutexRelease(fc->mutex);

        if (fan_active == 0U)
        {
            /* Đổi chế độ / PWM → reset bộ đếm xác nhận */
            if ((cur_duty != s_mon_last_duty) || (active_mode != s_mon_last_mode))
            {
                s_mon_last_duty   = cur_duty;
                s_mon_last_mode   = active_mode;
                s_mon_stable_tick = osKernelGetTickCount();
                s_fault_hits      = 0U;
            }

            /* Chỉ giám sát khi: có profile hợp lệ + không bị force_off
             *                   + duty > 0 + đã ổn định đủ thời gian  */
            bool warmup_ok = ((osKernelGetTickCount() - s_mon_stable_tick) >=
                              FAN_LEARN_MONITOR_WARMUP_MS);

            if ((fan_done != 0U) && (!force_off) && (cur_duty > 0U) &&
                warmup_ok && s_profile_valid(learned, cur_duty))
            {
                uint16_t exp_rpm = s_expected_rpm(learned, cur_duty);

                if (exp_rpm > 0U)
                {
                    uint16_t meas_rpm = s_measure_rpm_avg(fc, FAN_LEARN_RPM_AVG_SAMPLES);

                    FAN_LOG("Monitor: duty=%u%% meas=%u RPM exp=%u RPM\r\n",
                            (unsigned)cur_duty, (unsigned)meas_rpm, (unsigned)exp_rpm);

                    if (!s_rpm_within_tolerance(meas_rpm, exp_rpm,
                                                FAN_LEARN_TOLERANCE_PERCENT))
                    {
                        if (s_fault_hits < 255U) { s_fault_hits++; }
                        FAN_LOG("Monitor: out of tolerance (hit %u/%u)\r\n",
                                (unsigned)s_fault_hits,
                                (unsigned)FAN_LEARN_FAULT_CONFIRM_COUNT);

                        if (s_fault_hits >= FAN_LEARN_FAULT_CONFIRM_COUNT)
                        {
                            s_enter_learn_fault(fc, meas_rpm, exp_rpm, cur_duty);
                            s_fault_hits = 0U;
                        }
                    }
                    else
                    {
                        if (s_fault_hits > 0U)
                        {
                            FAN_LOG("Monitor: RPM back in tolerance\r\n");
                        }
                        s_fault_hits = 0U;

                        /* Xóa alarm nếu RPM đã về đúng (không xóa nếu còn low_speed_fault) */
                        osMutexAcquire(fc->mutex, osWaitForever);
                        bool clear = (fc->menu_ctx->fan.alarm_active != 0U) &&
                                     (fc->menu_ctx->fan.low_speed_fault == 0U);
                        osMutexRelease(fc->mutex);

                        if (clear)
                        {
                            s_recovery_learn_alarm(fc);
                        }
                    }
                }
            }
            else if (!s_profile_valid(learned, cur_duty))
            {
                /* Profile không hợp lệ (flash rác / chưa học) → không giám sát */
                s_fault_hits = 0U;
            }
        }

        s_monitor_tick = osKernelGetTickCount();
    }

    /* ================================================================
     * GIÁM SÁT AN TOÀN (1 s / lần)
     * ============================================================= */
    s_safety_monitor(fc, osKernelGetTickCount());
}

void fan_alarm_update(fan_ctx_t *fc, bool scd41_fault, uint8_t ds18b20_mask)
{
    bool on = false;

    osMutexAcquire(fc->mutex, osWaitForever);
    if (fan_has_any_fault_locked(&fc->menu_ctx->fan)) { on = true; }
    osMutexRelease(fc->mutex);

    if (scd41_fault)          { on = true; }
    if (ds18b20_mask != 0U)   { on = true; }

    if (fc->alarm_port != NULL)
    {
        HAL_GPIO_WritePin(fc->alarm_port, fc->alarm_pin,
                          on ? GPIO_PIN_SET : GPIO_PIN_RESET);
    }
}
