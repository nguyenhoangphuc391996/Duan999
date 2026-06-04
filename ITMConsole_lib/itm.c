/*
 * itm.c
 *
 *  Created on: Apr 24, 2026
 *      Author: embedded
 */

#include "itm.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* Runtime enable/disable table — all enabled by default */
static volatile uint8_t g_itm_lib_enabled[ITM_LIB_MAX];
static volatile uint8_t g_itm_inited = 0;

static void itm_ensure_init(void)
{
    if (!g_itm_inited)
    {
        for (int i = 0; i < ITM_LIB_MAX; ++i)
            g_itm_lib_enabled[i] = 1U;
        g_itm_inited = 1U;
    }
}

void itm_set_library_enabled(itm_lib_t lib, bool enabled)
{
    if (lib < 0 || lib >= ITM_LIB_MAX) return;
    itm_ensure_init();
    g_itm_lib_enabled[lib] = enabled ? 1U : 0U;
}

bool itm_is_library_enabled(itm_lib_t lib)
{
    if (lib < 0 || lib >= ITM_LIB_MAX) return false;
    itm_ensure_init();
    return (g_itm_lib_enabled[lib] != 0U);
}

void itm_print_library(itm_lib_t lib, const char* str)
{
    if (!itm_is_library_enabled(lib)) return;
    while (*str) { ITM_SendChar(*str++); }
}

void itm_put_int_library(itm_lib_t lib, int num)
{
    if (!itm_is_library_enabled(lib)) return;
    char buf[12];
    int i = 0;

    if (num == 0) { ITM_SendChar('0'); return; }

    if (num < 0) {
        ITM_SendChar('-');
        long long tmp = -(long long)num;
        while (tmp > 0 && i < (int)sizeof(buf)-1) { buf[i++] = (char)((tmp % 10) + '0'); tmp /= 10; }
    } else {
        unsigned int tmp = (unsigned int)num;
        while (tmp > 0 && i < (int)sizeof(buf)-1) { buf[i++] = (char)((tmp % 10) + '0'); tmp /= 10; }
    }

    while (i > 0) { ITM_SendChar(buf[--i]); }
}

void itm_printf_library(itm_lib_t lib, const char* fmt, ...)
{
    if (!itm_is_library_enabled(lib)) return;
    char tmp[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    itm_print_library(lib, tmp);
}

/* Backward-compatible wrappers → ITM_LIB_GLOBAL */
void itm_print(const char* str) { itm_print_library(ITM_LIB_GLOBAL, str); }

void itm_put_int(int num) { itm_put_int_library(ITM_LIB_GLOBAL, num); }

void Error_Handler_Debug(char *file, int line)
{
    __disable_irq();
    itm_print_library(ITM_LIB_GLOBAL, "\r\n[!] CRITICAL ERROR\r\n");
    itm_print_library(ITM_LIB_GLOBAL, "File: ");
    itm_print_library(ITM_LIB_GLOBAL, file);
    itm_print_library(ITM_LIB_GLOBAL, "\r\nLine: ");
    itm_put_int_library(ITM_LIB_GLOBAL, line);
    itm_print_library(ITM_LIB_GLOBAL, "\r\n------------------\r\n");
    while (1) {}
}