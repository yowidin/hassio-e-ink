/**
 * @file   shutdown.cpp
 * @author Dennis Sitelew
 * @date   Oct. 20, 2024
 */

#include <power-ic/shutdown.hpp>

#include <autoconf.h>
#include <zephyr/kernel.h>

std::chrono::seconds power_ic::shutdown::get_sleep_duration() {

   // TODO
   k_sleep(K_SECONDS(30));

   return std::chrono::seconds(CONFIG_APP_WAKE_UP_INTERVAL);
}
