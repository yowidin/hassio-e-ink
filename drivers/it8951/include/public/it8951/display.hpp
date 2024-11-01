/**
 * @file   display.hpp
 * @author Dennis Sitelew
 * @date   Oct. 25, 2024
 */

#pragma once

#include <it8951/common.hpp>

#include <array>
#include <cstdint>
#include <functional>
#include <span>
#include <system_error>

#include <zephyr/kernel.h>

namespace it8951 {

class display {
public:
   using pixel_data_t = std::span<const std::uint8_t>;

   //! @return 4bpp pixel value for the specified position (x, y)
   using pixel_func_t = std::function<std::uint8_t(std::uint16_t x, std::uint16_t y)>;

public:
   display(const device &device);
   display(const display &) = delete;
   display(display &&) = default;
   ~display() = default;

public:
   display &operator=(const display &) = delete;
   display &operator=(display &&) = default;

public:
   void begin(common::image::area a, common::image::config cfg, std::error_code &ec);
   void begin(common::image::area a, common::image::config cfg);

   void update(pixel_data_t data, std::error_code &ec);
   void update(pixel_data_t data);

   void end(std::error_code &ec);
   void end();

   void fill_screen(pixel_func_t generator, common::waveform_mode mode, std::error_code &ec);
   void fill_screen(pixel_func_t generator, common::waveform_mode mode);

   void clear(std::error_code &ec);
   void clear();

   std::uint16_t width() const;
   std::uint16_t height() const;

   common::image::area full_screen() const;
   common::image::config with_mode(common::waveform_mode mode) const;

private:
   const device *device_;
   common::image::area current_area_;
   common::image::config current_config_;

   std::array<std::uint8_t, CONFIG_EPD_BURST_WRITE_BUFFER_SIZE> fill_buffer_;
};

} // namespace it8951
