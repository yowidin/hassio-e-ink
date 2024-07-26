/**
 * @file   driver.c
 * @author Dennis Sitelew
 * @date   Jul. 25, 2024
 */

#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>

#include <it8951/driver.h>

#define DT_DRV_COMPAT ite_it8951

LOG_MODULE_REGISTER(it8951, CONFIG_IT8951_LOG_LEVEL);

static int check_and_init_pin(const struct gpio_dt_spec *pin, int extra_flags, const char *name) {
   if (!gpio_is_ready_dt(pin)) {
      LOG_WRN("%s pin not ready", name);
      return -ENODEV;
   }

   int err = gpio_pin_configure_dt(pin, extra_flags);
   if (err) {
      LOG_WRN("%s pin configure failed: %d", name, err);
      return -ENODEV;
   }

   return 0;
}

static int check_and_init_input_pin(const struct gpio_dt_spec *pin, const char *name) {
   return check_and_init_pin(pin, GPIO_INPUT, name);
}

static int check_and_init_output_pin(const struct gpio_dt_spec *pin, int initial_state, const char *name) {
   int err = check_and_init_pin(pin, GPIO_OUTPUT, name);
   if (err) {
      return err;
   }

   err = gpio_pin_set_dt(pin, initial_state);
   if (err) {
      LOG_WRN("%s pin set failed: %d", name, err);
      return -ENODEV;
   }

   return 0;
}

static int it8951_init(const struct device *dev) {
   const it8951_config_t *cfg = dev->config;

   int err = check_and_init_input_pin(&cfg->ready_pin, "RDY");
   if (err) {
      return -ENODEV;
   }

   err = check_and_init_output_pin(&cfg->reset_pin, 0, "RESET");
   if (err) {
      return -ENODEV;
   }

   err = check_and_init_output_pin(&cfg->cs_pin, 0, "CS");
   if (err) {
      return -ENODEV;
   }

   if (!spi_is_ready_dt(&cfg->spi)) {
      LOG_WRN("EPD SPI not ready");
      return -ENODEV;
   }

   return 0;
}

#define INIT(n)                                                                                                        \
   static it8951_data_t inst_##n##_data = {0};                                                                         \
   static const it8951_config_t inst_##n##_config = {                                                                  \
       .spi = SPI_DT_SPEC_INST_GET(                                                                                    \
           n, SPI_OP_MODE_MASTER | SPI_TRANSFER_MSB | SPI_WORD_SET(8) | SPI_LOCK_ON | SPI_HOLD_ON_CS, 0),              \
       .ready_pin = GPIO_DT_SPEC_INST_GET(n, ready_gpios),                                                             \
       .reset_pin = GPIO_DT_SPEC_INST_GET(n, reset_gpios),                                                             \
       .cs_pin = GPIO_DT_SPEC_INST_GET(n, cs_gpios),                                                                   \
       .vcom = DT_INST_PROP(n, vcom),                                                                                  \
   };                                                                                                                  \
   DEVICE_DT_INST_DEFINE(n, &it8951_init, PM_DEVICE_DT_INST_GET(n), &inst_##n##_data, &inst_##n##_config, POST_KERNEL, \
                         CONFIG_EPD_IT8951_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(INIT)
