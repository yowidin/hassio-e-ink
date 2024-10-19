#include <autoconf.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/reboot.h>

#include <hei/fuel_gauge.h>
#include <hei/display.hpp>
#include <hei/http/server.hpp>
#include <hei/image_client.hpp>
#include <hei/wifi.hpp>

#include <exception>

LOG_MODULE_REGISTER(main, CONFIG_APP_LOG_LEVEL);

static const struct gpio_dt_spec led_gpio = GPIO_DT_SPEC_GET(DT_NODELABEL(led), gpios);

bool led_init() {
   if (!device_is_ready(led_gpio.port)) {
      LOG_ERR("LED GPIO device isn't ready");
      return false;
   }

   int err = gpio_pin_configure_dt(&led_gpio, GPIO_OUTPUT_INACTIVE);
   if (err) {
      LOG_ERR("gpio_pin_configure_dt for LED failed: %d", err);
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

int fatal_error(const char *msg) {
   LOG_ERR("Fatal error: %s", msg);

   for (int i = 0; i < 12; ++i) {
      // TODO: toggle LED
      k_sleep(K_SECONDS(5));
   }

   sys_reboot(SYS_REBOOT_COLD);
   return -1;
}

void setup_connectivity() {
   try {
      hei::wifi::setup();
   } catch (const std::exception &e) {
      // Neither connection nor AP could work: we can't to anything: go into error mode
      LOG_ERR("Wi-Fi setup error: %s", e.what());
      fatal_error("Wi-Fi configuration failed");
   }
}

int main() {
   if (!led_init()) {
      return fatal_error("LED init failed");
   }

   if (!hei::display::init()) {
      return fatal_error("Display initialization failed");
   }

   setup_connectivity();

   k_sleep(K_MSEC(500));

   hei::http::server::start();
   if (!hei::wifi::is_hosting()) {
      hei::image_client::start();
   }
}
