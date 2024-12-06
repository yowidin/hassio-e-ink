/**
 * @file   display.cpp
 * @author Dennis Sitelew
 * @date   Oct. 27, 2024
 */

#include <it8951/display.hpp>
#include <it8951/hal.hpp>

#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(it8951, CONFIG_IT8951_LOG_LEVEL);

using namespace it8951;
using namespace zephyr;

display::display(const device &device)
   : device_{&device}
   , current_area_{}
   , current_config_{} {
   // Nothing to do here
}

void_t display::begin(common::image::area a, common::image::config cfg) {
   current_area_ = a;
   current_config_ = cfg;
   return hal::image::begin(*device_, a, cfg);
}

void_t display::update(pixel_data_t data) {
   return hal::write_data_chunked_bursts(*device_, data);
}

void_t display::end() {
   return hal::image::end(*device_, current_area_, current_config_.mode);
}

void_t display::fill_screen(pixel_func_t generator, common::waveform_mode mode) {
   const auto &data = get_data(*device_);

   const common::image::area area = {
      .x = 0,
      .y = 0,
      .width = data.info.panel_width,
      .height = data.info.panel_height,
   };

   const common::image::config config = {
      .endianness = common::endianness::big,
      .pixel_format = common::pixel_format::pf4bpp,
      .rotation = common::rotation::rotate0,
      .mode = mode,
   };

   // Set up
   auto br = begin(area, config);
   if (!br) {
      return br;
   }

   // Transfer data
   std::uint16_t offset = 0;
   static_assert((CONFIG_EPD_BURST_WRITE_BUFFER_SIZE % 2) == 0);
   for (std::uint16_t y = 0; y < data.info.panel_height; ++y) {
      for (std::uint16_t x = 0; x < data.info.panel_width; x += 2) {
         const auto p0 = generator(x, y);
         const auto p1 = generator(x + 1, y);
         fill_buffer_[offset++] = static_cast<std::uint8_t>(p0 << 4 | p1); // Big endian 4-bit pixels

         if (offset == fill_buffer_.size()) {
            auto res = hal::write_data_chunked_bursts(*device_, fill_buffer_);
            if (!res) {
               return res;
            }

            offset = 0;
         }
      }
   }

   // Write the rest of the data
   if (offset != 0) {
      auto res = hal::write_data_chunked_bursts(*device_, {fill_buffer_.data(), offset});
      if (!res) {
         return res;
      }
   }

   // Flush
   return end();
}

void_t display::clear() {
   return fill_screen(
      [](auto x, auto y) {
         return 0xFF;
      },
      common::waveform_mode::init);
}

void_t display::shutdown() {
   return hal::system::sleep(*device_);
}

std::uint16_t display::width() const {
   return get_data(*device_).info.panel_width;
}

std::uint16_t display::height() const {
   return get_data(*device_).info.panel_height;
}

common::image::area display::full_screen() const {
   const auto &data = get_data(*device_);
   return {
      .x = 0,
      .y = 0,
      .width = data.info.panel_width,
      .height = data.info.panel_height,
   };
}

common::image::config display::with_mode(common::waveform_mode mode) const {
   return {
      .endianness = common::endianness::big,
      .pixel_format = common::pixel_format::pf4bpp,
      .rotation = common::rotation::rotate0,
      .mode = mode,
   };
}
