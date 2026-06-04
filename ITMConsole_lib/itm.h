/*
 * itm.h
 *
 *  Created on: Apr 24, 2026
 *      Author: embedded
 */

#ifndef ITM_H_
#define ITM_H_

#include "stm32f1xx.h"
#include <stdbool.h>

/* Runtime-controlled per-library ITM logging.
 * Libraries call the "_library" variants with their library id.
 * For backward compatibility, itm_print/itm_put_int map to ITM_LIB_GLOBAL.
 */
typedef enum {
    ITM_LIB_GLOBAL = 0,
    ITM_LIB_SCD41,
    ITM_LIB_DS18B20,
    ITM_LIB_RTRECD,
    ITM_LIB_LCD,
    ITM_LIB_MENU,
    ITM_LIB_MAX
} itm_lib_t;

/* Control functions */
void itm_set_library_enabled(itm_lib_t lib, bool enabled);
bool itm_is_library_enabled(itm_lib_t lib);

/* Library-aware print helpers */
void itm_print_library(itm_lib_t lib, const char* str);
void itm_put_int_library(itm_lib_t lib, int num);
void itm_printf_library(itm_lib_t lib, const char* fmt, ...);

/* Backward-compatible plain helpers (map to ITM_LIB_GLOBAL) */
void itm_print(const char* str);
void itm_put_int(int num);
void Error_Handler_Debug(char *file, int line);

#define Error_Handler() Error_Handler_Debug(__FILE__, __LINE__)

#endif /* ITM_H_ */