/**
 * @file   fuel_gauge.h
 * @author Dennis Sitelew
 * @date   Jul. 24, 2024
 *
 * The fuel gauge driver is not compatible with C++, so we have to wrap it in C instead
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include <stdbool.h>
#include <stdint.h>

typedef struct hei_fuel_gauge_measurement {
   bool valid;
   uint32_t runtime_to_empty_minutes;
   uint32_t runtime_to_full_minutes;
   uint8_t relative_state_of_charge_percentage;
   uint32_t voltage_uv;
} hei_fuel_gauge_measurement_t;

bool hei_fuel_gauge_init(void);

void hei_fuel_gauge_print(void);

hei_fuel_gauge_measurement_t hei_fuel_gauge_get(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */
