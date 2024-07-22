#include <autoconf.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <esp_sleep.h>

LOG_MODULE_REGISTER(main, CONFIG_APP_LOG_LEVEL);

static const struct gpio_dt_spec led_gpio = GPIO_DT_SPEC_GET(DT_NODELABEL(led), gpios);

bool led_init() {
   if (!device_is_ready(led_gpio.port)) {
      LOG_ERR("LED GPIO device isn't ready");
      return false;
   }

   int err = gpio_pin_configure_dt(&led_gpio, GPIO_OUTPUT_INACTIVE);
   if (err) {
      LOG_ERR("gpio_pin_configure_dt for cloud LED failed: %d", err);
      return false;
   }

   return true;
}

void led_state(bool is_on) {
   int err = gpio_pin_set_dt(&led_gpio, is_on);
   if (err) {
      LOG_ERR("gpio_pin_set_dt for LED failed: %d", err);
   }
}

void light_sleep(k_timeout_t timeout) {
   while (true) {
      if (K_TIMEOUT_EQ(timeout, K_NO_WAIT)) {
         return;
      }

      const auto min_residency_us = DT_PROP(DT_NODELABEL(light_sleep), min_residency_us);
      const auto sleep_us = k_ticks_to_us_ceil64(timeout.ticks);
      if (sleep_us < min_residency_us) {
         // No point in starting the light sleep: the sleep duration is smaller than the minimal sleep amount required
         // for a light sleep.
         k_sleep(timeout);
         return;
      }

      if (auto err = esp_sleep_enable_timer_wakeup(sleep_us) != ESP_OK) {
         LOG_ERR("Timer wake up setup failed: %d", err);
      }

      // Actual sleep
      const auto ticks_before = k_uptime_ticks();
      const auto extra_sleep_overhead = 50;
      k_sleep(K_USEC(min_residency_us + extra_sleep_overhead));

      // Handle spurious wake-ups
      const auto ticks_after = k_uptime_ticks();
      const auto ticks_delta = ticks_after - ticks_before;
      const auto actual_us = k_ticks_to_us_ceil64(ticks_delta);

      if (actual_us >= sleep_us) {
         break;
      }

      timeout = K_USEC(sleep_us - actual_us);
   }
}

int main() {
   LOG_DBG("Zephyr Example Application %s", CONFIG_BOARD);
   if (!led_init()) {
      LOG_ERR("LED Init failed");
      return -1;
   }

   int counter = 0;
   bool is_on = true;
   while (true) {
      light_sleep(K_SECONDS(5));

      LOG_INF("Counter: %d", counter++);
      led_state(is_on);
      is_on = !is_on;
   }

   return 0;
}
