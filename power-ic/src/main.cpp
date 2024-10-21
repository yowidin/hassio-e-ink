#include <power-ic/led.hpp>
#include <power-ic/power.hpp>
#include <power-ic/shutdown.hpp>

#include <autoconf.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(main, CONFIG_APP_LOG_LEVEL);

int main(void) {
   using namespace power_ic;

   auto set_power_state = [](bool is_on) {
      power::set_state(is_on);
#if CONFIG_APP_LED_SIGNAL_POWER_STATE
      led::set_state(is_on);
#endif
   };

   while (true) {
      try {
         set_power_state(true);
         auto sleep_duration = shutdown::get_sleep_duration();

         set_power_state(false);

         k_sleep(K_SECONDS(sleep_duration.count()));
      } catch (const std::exception &e) {
         LOG_ERR("Exception: %s", e.what());
         k_sleep(K_SECONDS(CONFIG_APP_WAKE_UP_INTERVAL));
      }
   }
}