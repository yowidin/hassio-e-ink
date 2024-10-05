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

#include <cctype>
#include <cstdint>

LOG_MODULE_DECLARE(it8951, CONFIG_IT8951_LOG_LEVEL);

void it8951::begin(const device &dev, std::uint16_t width, std::uint16_t height) {
   hal::image::begin(dev, width, height);
}

void it8951::update(const device &dev, std::span<const std::uint8_t> data) {
   hal::image::update(dev, data);
}

void it8951::end(const device &dev, std::uint16_t width, std::uint16_t height, refresh type) {
   waveform_mode_t mode;
   switch (type) {
      case refresh::image:
         mode = waveform_mode_t::grayscale_limited_reduced;
         break;

      case refresh::full:
      default:
         mode = waveform_mode_t::init;
         break;
   }

   hal::image::end(dev, width, height, mode);
}

void it8951::clear(const device &dev) {
   hal::fill_screen(
      dev,
      [](auto x, auto y) {
         return 0xF0;
      },
      waveform_mode_t::init);
}
