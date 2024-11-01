/**
 * @file   spi.cpp
 * @author Dennis Sitelew
 * @date   Nov. 01, 2024
 */

#include <zephyr-cpp/drivers/spi.hpp>

#include <zephyr-cpp/error.hpp>

#include <zephyr/logging/log.h>

#include <cstdint>

LOG_MODULE_REGISTER(zpp_spi, CONFIG_ZEPHYR_CPP_LOG_LEVEL);

namespace zephyr::spi {

void_t ready(const spi_dt_spec &spec) {
   if (!spi_is_ready_dt(&spec)) {
      LOG_ERR("SPI bus %s not ready", spec.bus->name);
      return unexpected(ENODEV);
   }

   return {};
}

void_t write(const spi_dt_spec &spec, const spi_buf_set &buf_set) {
   if (auto err = spi_write_dt(&spec, &buf_set)) {
      LOG_ERR("SPI write failed on %s: %d", spec.bus->name, err);
      return unexpected(err);
   }

   return {};
}

void_t read(const spi_dt_spec &spec, const spi_buf_set &buf_set) {
   if (auto err = spi_read_dt(&spec, &buf_set)) {
      LOG_ERR("SPI read failed on %s: %d", spec.bus->name, err);
      return unexpected(err);
   }

   return {};
}

} // namespace zephyr::spi
