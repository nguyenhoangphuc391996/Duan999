/*
 * scd41_print.c
 *
 *  Created on: Apr 28, 2026
 */

#include "scd41_print.h"
#include "itm.h"

/* Route all itm_print/itm_put_int calls in this TU through ITM_LIB_SCD41 */
#undef  itm_print
#define itm_print(s)   itm_print_library(ITM_LIB_SCD41, (s))
#undef  itm_put_int
#define itm_put_int(n) itm_put_int_library(ITM_LIB_SCD41, (n))

void scd41_print_signed_milli(int32_t value)
{
  uint32_t abs_value;

  if (value < 0)
  {
    itm_print("-");
    abs_value = (uint32_t)(-(int64_t)value);
  }
  else
  {
    abs_value = (uint32_t)value;
  }

  itm_put_int((int)(abs_value / 1000U));
  itm_print(".");
  if (abs_value % 1000U < 100)
  {
    itm_print("0");
  }
  if (abs_value % 1000U < 10)
  {
    itm_print("0");
  }
  itm_put_int((int)(abs_value % 1000U));
}

void scd41_print_scd41_measurement(uint16_t co2,
                                   int32_t temperature_m_deg_c,
                                   int32_t humidity_m_percent_rh)
{
  itm_print("[SCD41] CO2=");
  itm_put_int((int)co2);
  itm_print(" ppm, T=");
  scd41_print_signed_milli(temperature_m_deg_c);
  itm_print(" C, RH=");
  scd41_print_signed_milli(humidity_m_percent_rh);
  itm_print(" %\r\n");
}
