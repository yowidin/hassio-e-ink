#include <autoconf.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/reboot.h>

#include <hei/fuel_gauge.h>
#include <hei/wifi.hpp>
#include <hei/server.hpp>

#if CONFIG_EPD_IT8951
#include <it8951/it8951.hpp>
#endif

#include <exception>

LOG_MODULE_REGISTER(main, CONFIG_APP_LOG_LEVEL);

#if CONFIG_EPD_IT8951
static const struct device *const display_driver = DEVICE_DT_GET_ONE(ite_it8951);
#endif

bool test_display_driver() {
#if CONFIG_EPD_IT8951
   if (!device_is_ready(display_driver)) {
      LOG_ERR("Display not ready: %s", display_driver->name);
      return false;
   }

   try {
      it8951::display_dummy_image(*display_driver);
   } catch (const std::exception &e) {
      LOG_ERR("Display image failed: %s", e.what());
      return false;
   }
#endif

   return true;
}

void fatal_error(const char *msg) {
   LOG_ERR("Fatal error: %s", msg);

   for (int i = 0; i < 12; ++i) {
      // TODO: toggle LED
      k_sleep(K_SECONDS(5));
   }

   sys_reboot(SYS_REBOOT_COLD);
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
   if (!hei_fuel_gauge_init()) {
      LOG_ERR("Fuel gauge init failed");
   }

   setup_connectivity();

   k_sleep(K_MSEC(500));

   hei::server::start();

   // test_display_driver();

   int counter = 0;
   while (true) {
      k_sleep(K_SECONDS(5));

      LOG_INF("Counter: %d", counter++);
      hei_fuel_gauge_print();
   }
}
