/*
 * scd41_print.h
 *
 *  Created on: Apr 28, 2026
 */

#ifndef SCD41_PRINT_H_
#define SCD41_PRINT_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

void scd41_print_signed_milli(int32_t value);
void scd41_print_scd41_measurement(uint16_t co2,
                                   int32_t temperature_m_deg_c,
                                   int32_t humidity_m_percent_rh);

#ifdef __cplusplus
}
#endif

#endif /* SCD41_PRINT_H_ */
