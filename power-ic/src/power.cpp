/**
 * @file   power.cpp
 * @author Dennis Sitelew
 * @date   Oct. 20, 2024
 */

#include <power-ic/power.hpp>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(power, CONFIG_APP_LOG_LEVEL);

namespace {

const gpio_dt_spec power_dt = GPIO_DT_SPEC_GET(DT_NODELABEL(power_switch), gpios);

extern "C" {

int init_power(void) {
   if (!gpio_is_ready_dt(&power_dt)) {
      LOG_ERR("Power switch GPIO not ready");
      return -ENODEV;
   }

   if (const auto err = gpio_pin_configure_dt(&power_dt, GPIO_OUTPUT_INACTIVE) < 0) {
      LOG_ERR("Power switch GPIO config failed: %d", err);
      return err;
   }

   return 0;
}

} // extern "C"

} // namespace

void power_ic::power::set_state(bool desired) {
   LOG_INF("Power %s request", (desired ? "ON" : "OFF"));

   if (const auto err = gpio_pin_set_dt(&power_dt, desired) < 0) {
      LOG_ERR("Power switch state set failed: %d", err);
   }
}

SYS_INIT(init_power, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
