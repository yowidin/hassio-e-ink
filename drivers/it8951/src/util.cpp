/**
 * @file   util.cpp
 * @author Dennis Sitelew
 * @date   Jul. 31, 2024
 */

#include <it8951/util.hpp>

#include <zephyr/logging/log.h>

#include <cstdint>

LOG_MODULE_DECLARE(it8951, CONFIG_IT8951_LOG_LEVEL);

namespace it8951 {

namespace pin {

void check(const gpio_dt_spec &spec) {
   if (!gpio_is_ready_dt(&spec)) {
      LOG_ERR("GPIO port %s not ready", spec.port->name);
      throw std::system_error(error::hal_error);
   }
}

void configure(const gpio_dt_spec &spec, gpio_flags_t flags) {
   if (auto err = gpio_pin_configure_dt(&spec, flags)) {
      LOG_ERR("Pin configure failed for %s: %d", spec.port->name, err);
      throw std::system_error(error::hal_error);
   }
}

void interrupt_configure(const gpio_dt_spec &spec, gpio_flags_t flags) {
   if (auto err = gpio_pin_interrupt_configure_dt(&spec, flags)) {
      LOG_ERR("Pin interrupt configure failed for %s: %d", spec.port->name, err);
      throw std::system_error(error::hal_error);
   }
}

void add_callback(const gpio_dt_spec &spec, gpio_callback &cb) {
   if (auto err = gpio_add_callback_dt(&spec, &cb)) {
      LOG_ERR("Add callback failed for %s: %d", spec.port->name, err);
      throw std::system_error(error::hal_error);
   }
}

void set(const gpio_dt_spec &spec, bool logic_high, std::error_code &ec) {
   const auto value = logic_high ? 1 : 0;
   if (int err = gpio_pin_set_dt(&spec, value)) {
      LOG_ERR("Error setting pin %d of %s to %d: %d", static_cast<int>(spec.pin), spec.port->name, value, err);
      ec = error::hal_error;
   }
   ec = error::success;
}

void set(const gpio_dt_spec &spec, bool logic_high) {
   std::error_code ec;
   set(spec, logic_high, ec);
   if (ec) {
      throw std::system_error(ec);
   }
}

bool get(const gpio_dt_spec &spec) {
   int res = gpio_pin_get_dt(&spec);
   switch (res) {
      case 0:
         return false;

      case 1:
         return true;

      default:
         LOG_ERR("Error getting pin %d of %s: %d", static_cast<int>(spec.pin), spec.port->name, res);
         throw std::system_error(error::hal_error);
   }
}

} // namespace pin

namespace spi {

void write(const spi_dt_spec &spec, const spi_buf_set &buf_set) {
   if (auto err = spi_write_dt(&spec, &buf_set)) {
      LOG_ERR("SPI write failed on %s: %d", spec.bus->name, err);
      throw std::system_error(error::transport_error);
   }
}

void read(const spi_dt_spec &spec, const spi_buf_set &buf_set) {
   if (auto err = spi_read_dt(&spec, &buf_set)) {
      LOG_ERR("SPI read failed on %s: %d", spec.bus->name, err);
      throw std::system_error(error::transport_error);
   }
}

} // namespace spi

} // namespace it8951
