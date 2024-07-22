#include <autoconf.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

#include <esp_sleep.h>

LOG_MODULE_REGISTER(main, CONFIG_APP_LOG_LEVEL);

struct config {
   bool valid;
   int counter;
};

static config cfg = {.valid = false, .counter = 0};

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
#if CONFIG_PM
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
#else
   k_sleep(timeout);
#endif
}

static int settings_set_handler(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg) {
   const char *next;
   if (settings_name_steq(name, "settings", &next) && !next) {
      if (len != sizeof(config)) {
         return -EINVAL;
      }

      int rc = read_cb(cb_arg, &cfg, sizeof(config));
      if (rc >= 0) {
         LOG_INF("Restored settings: %d / %d", cfg.valid, cfg.counter);
         return 0;
      }

      return rc;
   }

   return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(hei_handler, "hei", NULL, settings_set_handler, NULL, NULL);

int main() {
   LOG_DBG("Zephyr Example Application %s", CONFIG_BOARD);
   if (!led_init()) {
      LOG_ERR("LED Init failed");
      return -1;
   }

   if (auto err = settings_subsys_init()) {
      LOG_ERR("Settings initialization failed: %d", err);
      return -1;
   }

   if (auto err = settings_load_subtree("hei")) {
      LOG_ERR("Settings load failed: %d", err);
      return -1;
   }

   bool is_on = true;
   while (true) {
      light_sleep(K_SECONDS(5));
      led_state(is_on);
      is_on = !is_on;

      ++cfg.counter;
      LOG_INF("Counter: %d", cfg.counter);

      if (cfg.counter % 5 == 0) {
         cfg.valid = true;
         if (auto err = settings_save_one("hei/settings", &cfg, sizeof(cfg))) {
            LOG_ERR("Settings save failed: %d", err);
            return -1;
         } else {
            LOG_DBG("Saved settings: %d", cfg.counter);
         }
      }
   }
}
