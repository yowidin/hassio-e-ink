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
#include <hei/led.hpp>
#include <hei/wifi.hpp>

LOG_MODULE_REGISTER(main, CONFIG_APP_LOG_LEVEL);

[[noreturn]] void fatal_error(const char *msg) {
   LOG_ERR("Fatal error: %s", msg);

   bool state = true;
   for (int i = 0; i < 15; ++i) {
      (void)hei::led::set_state(state);
      state = !state;

      k_sleep(K_MSEC(200));
   }

   sys_reboot(SYS_REBOOT_COLD);
}

void setup_connectivity() {
   auto res = hei::wifi::setup();
   if (!res) {
      // Neither connection nor AP could work: we can't to anything: go into error mode
      LOG_ERR("Wi-Fi setup error: %s", res.error().message().c_str());
      fatal_error("Wi-Fi configuration failed");
   }
}

int main() {
   if (!hei::display::init()) {
      fatal_error("Display initialization failed");
   }

   setup_connectivity();

   hei::http::server::start();
   if (!hei::wifi::is_hosting()) {
      hei::image_client::start();
   }
}
