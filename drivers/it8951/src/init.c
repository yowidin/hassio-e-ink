/**
 * @file   init.c
 * @author Dennis Sitelew
 * @date   Jul. 30, 2024
 *
 * Driver initialization is not C++ compatible, so we have to do it in a C-file
 */

#include <zephyr/devicetree.h>

#include <it8951/init.h>

#include <autoconf.h>

#define DT_DRV_COMPAT ite_it8951

#define INIT(n)                                                                                         \
   BUILD_ASSERT(DT_INST_PROP(n, vcom) <= UINT16_MAX);                                                   \
   static it8951_data_t inst_##n##_data = {0};                                                          \
   static const it8951_config_t inst_##n##_config = {                                                   \
      .spi = SPI_DT_SPEC_INST_GET(                                                                      \
         n, SPI_OP_MODE_MASTER | SPI_TRANSFER_MSB | SPI_WORD_SET(8) | SPI_LOCK_ON | SPI_HOLD_ON_CS, 0), \
      .ready_pin = GPIO_DT_SPEC_INST_GET(n, ready_gpios),                                               \
      .reset_pin = GPIO_DT_SPEC_INST_GET(n, reset_gpios),                                               \
      .cs_pin = GPIO_DT_SPEC_INST_GET(n, cs_gpios),                                                     \
      .vcom = DT_INST_PROP(n, vcom),                                                                    \
   };                                                                                                   \
   DEVICE_DT_INST_DEFINE(n, &it8951_init, NULL, &inst_##n##_data, &inst_##n##_config, POST_KERNEL,      \
                         CONFIG_EPD_IT8951_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(INIT)
