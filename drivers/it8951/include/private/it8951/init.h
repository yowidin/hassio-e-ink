/**
 * @file   init.h
 * @author Dennis Sitelew
 * @date   Jul. 30, 2024
 */

#pragma once

#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

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
   int16_t vcom;
} it8951_config_t;

typedef enum it8951_event {
   //! The chip is ready
   it8951_ready = BIT(0),

   //! The chip has encountered an error
   it8951_error = BIT(1),
} it8951_event_t;

typedef struct it8951_device_info {
   uint16_t panel_width;
   uint16_t panel_height;
   uint32_t image_buffer_address; // Bits 0-23
   char it8951_version[17];       // Includes \0
   char lut_version[17];          // Includes \0
} it8951_device_info_t;

typedef struct it8951_data {
   //! Current driver state
   struct k_event state;

   //! Ready pin interrupt callback
   struct gpio_callback ready_cb;

   //! Pointer back to the device itself.
   //! We need this because there is no other way of getting back from the interrupt callback to the device.
   const struct device *dev;

   //! Device information (will be filled out during initialization).
   it8951_device_info_t info;
} it8951_data_t;

int it8951_init(const struct device *dev);

#ifdef __cplusplus
}
#endif // __cplusplus
