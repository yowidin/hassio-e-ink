#include <autoconf.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/reboot.h>

#include <exception>

LOG_MODULE_REGISTER(main, CONFIG_APP_LOG_LEVEL);

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec power_switch = GPIO_DT_SPEC_GET(DT_NODELABEL(power_switch), gpios);
static const struct gpio_dt_spec shutdown_request = GPIO_DT_SPEC_GET(DT_NODELABEL(shutdown_request), gpios);

extern k_timer power_on;

static void set_power_state(bool is_on) {
   if (is_on) {
      LOG_INF("Power ON request");
      gpio_pin_set_dt(&led, true);
      gpio_pin_set_dt(&power_switch, true);
   } else {
      LOG_INF("Power OFF request");
      gpio_pin_set_dt(&led, false);
      gpio_pin_set_dt(&power_switch, false);
      k_timer_start(&power_on, K_SECONDS(10), K_NO_WAIT);
   }
}

static void handle_power_on_timer(k_timer *timer) {
   ARG_UNUSED(timer);
   set_power_state(true);
}

K_TIMER_DEFINE(power_on, handle_power_on_timer, nullptr);

static bool setup_power_switch() {
   if (!gpio_is_ready_dt(&power_switch)) {
      LOG_ERR("Power switch GPIO not ready");
      return false;
   }

   const auto ret = gpio_pin_configure_dt(&power_switch, GPIO_OUTPUT_INACTIVE);
   if (ret < 0) {
      LOG_ERR("Power switch GPIO config failed: %d", ret);
      return false;
   }

   return true;
}

static bool setup_shutdown_request() {
   if (!gpio_is_ready_dt(&shutdown_request)) {
      LOG_ERR("Shutdown request GPIO not ready");
      return false;
   }

   auto ret = gpio_pin_configure_dt(&shutdown_request, GPIO_INPUT | GPIO_PULL_DOWN);
   if (ret < 0) {
      LOG_ERR("Shutdown request GPIO config failed: %d", ret);
      return false;
   }

   ret = gpio_pin_interrupt_configure_dt(&shutdown_request, GPIO_INT_TRIG_HIGH);
   if (ret < 0) {
      LOG_ERR("Shutdown request interrupt configuration failed: %d", ret);
      return false;
   }

   static gpio_callback shutdown_cb{};
   gpio_init_callback(
      &shutdown_cb,
      [](const auto gpio, auto cb, auto pins) {
         ARG_UNUSED(gpio);
         ARG_UNUSED(cb);
         ARG_UNUSED(pins);
         set_power_state(false);
      },
      BIT(shutdown_request.pin));

   ret = gpio_add_callback_dt(&shutdown_request, &shutdown_cb);
   if (ret < 0) {
      LOG_ERR("Error adding shutdown request interrupt callback: %d", ret);
      return false;
   }

   return true;
}

static bool setup_led() {
   if (!gpio_is_ready_dt(&led)) {
      LOG_ERR("LED GPIO not ready");
      return false;
   }

   const auto ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
   if (ret < 0) {
      LOG_ERR("LED GPIO config failed: %d", ret);
      return false;
   }

   return true;
}

int main(void) {
   if (!setup_led()) {
      return -1;
   }

   if (!setup_power_switch()) {
      return -1;
   }

   if (!setup_shutdown_request()) {
      return -1;
   }

   LOG_DBG("Power IC started");
   k_timer_start(&power_on, K_SECONDS(10), K_NO_WAIT);
}