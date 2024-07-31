/**
 * @file   it8951.cpp
 * @author Dennis Sitelew
 * @date   Jul. 25, 2024
 */

#include <it8951/driver.hpp>

#include <it8951/error.hpp>
#include <it8951/hal.hpp>
#include <it8951/it8951.hpp>

#include <zephyr/logging/log.h>

#include <cstdint>

LOG_MODULE_DECLARE(it8951, CONFIG_IT8951_LOG_LEVEL);

void it8951::display_dummy_image(const device &dev) {
   hal::fill_screen(
      dev,
      [](auto x, auto y) {
         return 0xF0;
      },
      waveform_mode_t::mode0);

   k_sleep(K_SECONDS(10));

   hal::fill_screen(
      dev,
      [](auto x, auto y) -> std::uint8_t {
         std::uint8_t y_values[] = {
            0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE,
         };
         auto y_capped = y % 16;
         auto x_capped = x % 16;

         return x_capped + y_values[y_capped];
      },
      waveform_mode_t::mode2);

   k_sleep(K_SECONDS(10));

   hal::fill_screen(
      dev,
      [](auto x, auto y) {
         return 0xF0;
      },
      waveform_mode_t::mode0);
}
