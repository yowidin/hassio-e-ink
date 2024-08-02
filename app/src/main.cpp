#include <autoconf.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <hei/fuel_gauge.h>
#include <hei/settings.hpp>

#include <it8951/it8951.hpp>

#include <exception>

LOG_MODULE_REGISTER(main, CONFIG_APP_LOG_LEVEL);

static const struct device *const display_driver = DEVICE_DT_GET_ONE(ite_it8951);

bool test_display_driver() {
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

   return true;
}

int main() {
   if (!hei_fuel_gauge_init()) {
      LOG_ERR("Fuel gauge init failed");
   }

   if (hei::settings::configured()) {
      LOG_INF("Application is fully configured");
   } else {
      LOG_WRN("Application is not configured");
   }

   // test_display_driver();

   int counter = 0;
   while (true) {
      k_sleep(K_SECONDS(5));

      LOG_INF("Counter: %d", counter++);
      hei_fuel_gauge_print();
   }
}
