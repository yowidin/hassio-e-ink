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

bool hei_fuel_gauge_init(void);

void hei_fuel_gauge_print(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */
