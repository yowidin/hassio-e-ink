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

extern const unsigned char *get_image_pgm();
extern unsigned int get_image_len();

void it8951::display_dummy_image(const device &dev) {
   hal::system::sleep(dev);
   hal::system::power(dev, false);

   auto clear_screen = [&]() {
      hal::fill_screen(
         dev,
         [](auto x, auto y) {
            return 0xF0;
         },
         waveform_mode_t::init);
   };

   auto display_image = [&] {
      // Note: we are assuming correct image size here
      const auto *img = get_image_pgm();
      const auto expected_spaces = 4; // Magic\wWidth\wHeight\wDepth\wData
      const auto end = img + get_image_len();
      int new_lines = 0;
      while (new_lines < expected_spaces && img != end) {
         if (std::isspace(*img)) {
            ++new_lines;
         }
         ++img;
      }

      if (img == end) {
         LOG_ERR("Invalid image format");
         return;
      }

      hal::fill_screen(
         dev,
         [&](auto x, auto y) -> std::uint8_t {
            return img[static_cast<std::size_t>(y) * 1200U + static_cast<std::size_t>(x)];
         },
         waveform_mode_t::grayscale_limited_reduced);
   };

   // Leaving an image might lead to burn-ins, so just present it shortly and then clean the screen
   clear_screen();
   k_sleep(K_SECONDS(10));

   display_image();
   k_sleep(K_SECONDS(10));

   clear_screen();
}
