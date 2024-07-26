/**
 * @file   driver.h
 * @author Dennis Sitelew
 * @date   Jul. 25, 2024
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>

typedef struct it8951_config {
   //! SPI bus the device is assigned to
   struct spi_dt_spec spi;

   //! Ready pin GPIO specification
   struct gpio_dt_spec ready_pin;

   //! Reset pin GPIO specification
   struct gpio_dt_spec reset_pin;

   //! Real CS pin GPIO specification
   struct gpio_dt_spec cs_pin;

   //! Custom VCOM value
   int vcom;
} it8951_config_t;

typedef struct it8951_data {
   //! Placeholder for driver state
   int dummy;
} it8951_data_t;

#ifdef __cplusplus
}
#endif /* __cplusplus */
