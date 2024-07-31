/**
 * @file   hal.cpp
 * @author Dennis Sitelew
 * @date   Jul. 28, 2024
 */

#include <it8951/error.hpp>
#include <it8951/hal.hpp>
#include <it8951/util.hpp>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <optional>

#include <inttypes.h>

LOG_MODULE_REGISTER(it8951_hal, CONFIG_IT8951_LOG_LEVEL);

namespace {

using namespace it8951;

template <typename T>
inline auto u16(T v) {
   return static_cast<std::uint16_t>(v);
}

template <typename T>
inline auto u8(T v) {
   return static_cast<std::uint8_t>(v);
}

void wait_for_ready_state(const struct device &dev, k_timeout_t timeout = K_MSEC(CONFIG_EPD_READY_LINE_TIMEOUT)) {
   auto &data = get_data(dev);
   const uint32_t events = k_event_wait(&data.state, it8951_ready, false, timeout);
   if ((events & it8951_ready) == 0) {
      throw std::system_error{error::timeout};
   }
}

void wait_for_display_ready(const struct device &dev, k_timeout_t timeout = K_MSEC(CONFIG_EPD_DISPLAY_READY_TIMEOUT)) {
   const auto deadline = k_uptime_ticks() + timeout.ticks;
   while (k_uptime_ticks() < deadline) {
      auto reg = hal::read_register(dev, reg_t::lutafsr);
      if (reg == 0) {
         // We are done waiting
         return;
      }

      k_sleep(K_MSEC(1));
   }

   throw std::system_error{error::timeout};
}

class cs_control {
public:
   explicit cs_control(const device &dev)
      : cfg_{&get_config(dev)} {

      wait_for_ready_state(dev);
      pin::set(cfg_->cs_pin, true);
   }

   ~cs_control() {
      // We don't want to throw from destructor
      std::error_code ec;
      pin::set(cfg_->cs_pin, false, ec);
   }

private:
   const it8951_config_t *cfg_;
};

//! Write a single word (16 bits), assuming the CS line is held.
void write_word(const device &dev, std::uint16_t word) {
   std::array data{u8((word >> 8U) & 0xFF), u8(word & 0xFF)};
   const spi_buf tx_buf = {
      .buf = data.data(),
      .len = data.size(),
   };

   const struct spi_buf_set tx_buf_set = {
      .buffers = &tx_buf,
      .count = 1,
   };

   spi::write(get_config(dev).spi, tx_buf_set);
}

//! Write some data assuming the CS line is held.
void write(const device &dev, const_span_t data) {
   for (const auto word : data) {
      wait_for_ready_state(dev);
      write_word(dev, word);
   }
}

//! Read a single word (16 bits), assuming the CS line is held.
std::uint16_t read_word(const device &dev) {
   std::array<std::uint8_t, 2> data{};
   const spi_buf rx_buf = {
      .buf = data.data(),
      .len = data.size(),
   };

   const struct spi_buf_set rx_buf_set = {
      .buffers = &rx_buf,
      .count = 1,
   };

   spi::read(get_config(dev).spi, rx_buf_set);
   return u16(data[0]) << 8U | u16(data[1]);
}

// Read some data, assuming the CS line is already held down
void read(const device &dev, span_t data, bool discard_first_word = true) {
   if (data.empty() && !discard_first_word) {
      LOG_WRN("Empty read requested");
      return;
   }

   if (discard_first_word) {
      wait_for_ready_state(dev);
      (void)read_word(dev);
   }

   for (auto &word : data) {
      wait_for_ready_state(dev);
      word = read_word(dev);
   }
}

void set_image_buffer_base_address(const device &dev) {
   const auto &data = get_data(dev);
   const auto address = data.info.image_buffer_address;

   const auto high = u16((address >> 16U) & 0xFFFF);
   const auto low = u16(address & 0xFFFF);

   hal::write_register(dev, reg_t::lisar_high, high);
   hal::write_register(dev, reg_t::lisar_low, low);
}

void load_image_area_start(const device &dev, image_config cfg, image_area area) {
   std::array args{
      u16((u16(cfg.endianness) << 8) | (u16(cfg.pixel_format) << 4) | u16(cfg.rotation)),
      u16(area.x),
      u16(area.y),
      u16(area.width),
      u16(area.height),
   };
   hal::write_command(dev, command_t::load_image_area, args);
}

void load_image_end(const device &dev) {
   hal::write_command(dev, command_t::load_image_end);
}

void display_area(const device &dev, image_area area, waveform_mode_t mode) {
   std::array args{
      u16(area.x), u16(area.y), u16(area.width), u16(area.height), u16(mode),
   };
   hal::write_command(dev, command_t::display_area, args);
}

} // namespace

namespace it8951::hal {

void write_command(const device &dev, command_t cmd) {
   cs_control cs{dev};
   write(dev, {{u16(preamble_t::write_command), u16(cmd)}});
}

void write_command(const device &dev, command_t cmd, const_span_t args) {
   write_command(dev, cmd);

   for (auto word : args) {
      write_data(dev, {{word}});
   }
}

void write_data(const device &dev, const_span_t data) {
   cs_control cs{dev};
   write(dev, {{u16(preamble_t::write_data)}});
   write(dev, data);
}

void write_register(const device &dev, reg_t reg, std::uint16_t value) {
   write_command(dev, it8951::command_t::register_write);
   write_data(dev, {{u16(reg), value}});
}

void read_data(const device &dev, span_t data) {
   cs_control cs{dev};
   write(dev, {{u16(preamble_t::read_data)}});
   read(dev, data);
}

std::uint16_t read_register(const device &dev, reg_t reg) {
   write_command(dev, it8951::command_t::register_read);
   write_data(dev, {{u16(reg)}});

   std::array<std::uint16_t, 1> result{};
   read_data(dev, result);

   return result[0];
}

void enable_packed_mode(const device &dev) {
   write_register(dev, reg_t::i80cpcr, 0x0001);
}

namespace vcom {

std::uint16_t get(const device &dev) {
   write_command(dev, command_t::set_vcom);
   write_data(dev, {{u16(0)}});

   std::array<std::uint16_t, 1> result{};
   read_data(dev, result);

   return result[0];
}

void set(const device &dev, std::uint16_t value) {
   write_command(dev, command_t::set_vcom);
   write_data(dev, {{u16(1), value}});
}

} // namespace vcom

void fill_screen(const device &dev,
                 const std::function<std::uint8_t(std::uint16_t, std::uint16_t)> &generator,
                 waveform_mode_t mode) {
   wait_for_display_ready(dev);
   set_image_buffer_base_address(dev);

   const auto &data = get_data(dev);
   const image_area area{.x = 0, .y = 0, .width = data.info.panel_width, .height = data.info.panel_height};
   load_image_area_start(
      dev,
      {.endianness = endianness_t::little, .pixel_format = pixel_format_t::pf8bpp, .rotation = rotation_t::rotate0},
      area);

   std::array<std::uint8_t, CONFIG_EPD_BURST_WRITE_BUFFER_SIZE> buf{};

   auto burst_write_pixels = [&](std::size_t count) {
      std::uint16_t preamble = encoding::byte_swap(u16(preamble_t::write_data));

      std::array spi_buffers{
         spi_buf{.buf = &preamble, .len = sizeof(preamble)},
         spi_buf{.buf = buf.data(), .len = buf.size()},
      };

      // Swap pixels for big endian encoding
      for (std::size_t i = 0; i < count; i += 2) {
         auto tmp = buf[i];
         buf[i] = buf[i + 1];
         buf[i + 1] = tmp;
      }

      const struct spi_buf_set tx_buf_set = {
         .buffers = spi_buffers.data(),
         .count = spi_buffers.size(),
      };

      cs_control cs{dev};
      spi::write(get_config(dev).spi, tx_buf_set);
   };

   for (uint16_t y = 0; y < data.info.panel_height; ++y) {
      for (uint16_t x = 0; x < data.info.panel_width; x += buf.size()) {
         const auto remainder = std::min<std::size_t>(data.info.panel_width - x, buf.size());
         std::generate(std::begin(buf), std::end(buf), [&]() {
            return generator(x, y);
         });
         burst_write_pixels(remainder);
      }
   }

   load_image_end(dev);

   display_area(dev, area, mode);
}

} // namespace it8951::hal
