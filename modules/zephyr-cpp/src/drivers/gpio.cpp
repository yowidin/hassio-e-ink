/**
 * @file   gpio.cpp
 * @author Dennis Sitelew
 * @date   Nov. 01, 2024
 */

#include <zephyr-cpp/drivers/gpio.hpp>
#include <zephyr-cpp/error.hpp>

#include <zephyr/logging/log.h>

#include <cstdint>

LOG_MODULE_REGISTER(zpp_gpio, CONFIG_ZEPHYR_CPP_LOG_LEVEL);

namespace zephyr::gpio {

void_t ready(const gpio_dt_spec &spec) {
   if (!gpio_is_ready_dt(&spec)) {
      LOG_ERR("GPIO port %s not ready", spec.port->name);
      return unexpected(ENODEV);
   }

   return {};
}

void_t configure(const gpio_dt_spec &spec, gpio_flags_t extra_flags) {
   if (auto err = gpio_pin_configure_dt(&spec, extra_flags)) {
      LOG_ERR("Pin configure failed for %s: %d", spec.port->name, err);
      return unexpected(err);
   }

   return {};
}

void_t interrupt_configure(const gpio_dt_spec &spec, gpio_flags_t flags) {
   if (auto err = gpio_pin_interrupt_configure_dt(&spec, flags)) {
      LOG_ERR("Pin interrupt configure failed for %s: %d", spec.port->name, err);
      return unexpected(err);
   }

   return {};
}

void_t add_callback(const gpio_dt_spec &spec, gpio_callback &cb) {
   if (auto err = gpio_add_callback_dt(&spec, &cb)) {
      LOG_ERR("Add callback failed for %s: %d", spec.port->name, err);
      return unexpected(err);
   }

   return {};
}

void_t set(const gpio_dt_spec &spec, bool logic_high) {
   const auto value = logic_high ? 1 : 0;
   if (auto err = gpio_pin_set_dt(&spec, value)) {
      LOG_ERR("Error setting pin %d of %s to %d: %d", static_cast<int>(spec.pin), spec.port->name, value, err);
      return unexpected(err);
   }

   return {};
}

expected<bool> get(const gpio_dt_spec &spec) {
   if (const auto res = gpio_pin_get_dt(&spec); res == 0) {
      return false;
   } else if (res == 1) {
      return true;
   } else {
      LOG_ERR("Error getting pin %d of %s: %d", static_cast<int>(spec.pin), spec.port->name, res);
      return unexpected(res);
   }
}

} // namespace zephyr::gpio
