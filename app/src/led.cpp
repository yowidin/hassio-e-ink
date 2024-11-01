/**
 * @file   led.cpp
 * @author Dennis Sitelew
 * @date   Oct. 20, 2024
 */

#include <hei/led.hpp>

#include <zephyr-cpp/drivers/gpio.hpp>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(led, CONFIG_APP_LOG_LEVEL);

using namespace zephyr;

namespace {

const gpio_dt_spec led_dt = GPIO_DT_SPEC_GET(DT_NODELABEL(led), gpios);

void_t try_init() {
   return gpio::ready(led_dt).and_then([&] {
      return gpio::configure(led_dt, GPIO_OUTPUT_INACTIVE);
   });
}

extern "C" {

int init_led(void) {
   auto init_res = try_init();
   if (!init_res) {
      return init_res.error().value();
   }
   return 0;
}

} // extern "C"

} // namespace

void_t hei::led::set_state(bool desired) {
   return gpio::set(led_dt, desired);
}

SYS_INIT(init_led, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);