/**
 * @file   led.cpp
 * @author Dennis Sitelew
 * @date   Oct. 20, 2024
 */

#include <hei/led.hpp>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(led, CONFIG_APP_LOG_LEVEL);

namespace {

const gpio_dt_spec led_dt = GPIO_DT_SPEC_GET(DT_NODELABEL(led), gpios);

extern "C" {

int init_led(void) {
   if (!gpio_is_ready_dt(&led_dt)) {
      LOG_ERR("LED GPIO not ready");
      return -ENODEV;
   }

   if (const auto err = gpio_pin_configure_dt(&led_dt, GPIO_OUTPUT_INACTIVE) < 0) {
      LOG_ERR("LED GPIO config failed: %d", err);
      return err;
   }

   return 0;
}

} // extern "C"

} // namespace

void hei::led::set_state(bool desired) {
   if (const auto err = gpio_pin_set_dt(&led_dt, desired) < 0) {
      LOG_ERR("LED GPIO set failed: %d", err);
   }
}

SYS_INIT(init_led, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);