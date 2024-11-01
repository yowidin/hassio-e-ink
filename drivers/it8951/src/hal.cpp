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

void wait_for_ready_state(const struct device &dev,
                          std::error_code &ec,
                          k_timeout_t timeout = K_MSEC(CONFIG_EPD_READY_LINE_TIMEOUT)) {
   auto &data = get_data(dev);
   const uint32_t events = k_event_wait(&data.state, it8951_ready, false, timeout);
   if ((events & it8951_ready) == 0) {
      ec = error::timeout;
   }
}

void wait_for_display_ready(const struct device &dev,
                            std::error_code &ec,
                            k_timeout_t timeout = K_MSEC(CONFIG_EPD_DISPLAY_READY_TIMEOUT)) {
   const auto deadline = k_uptime_ticks() + timeout.ticks;
   while (k_uptime_ticks() < deadline) {
      auto reg = hal::read_register(dev, hal::reg::lutafsr, ec);
      if (ec) {
         return;
      }

      if (reg && *reg == 0) {
         // We are done waiting
         return;
      }

      k_sleep(K_MSEC(1));
   }

   ec = error::timeout;
}

class cs_control {
private:
   cs_control(const it8951_config_t &cfg)
      : cfg_{&cfg} {
      // Nothing to do here
   }

public:
   cs_control(const cs_control &) = delete;

   cs_control(cs_control &&o)
      : cfg_{o.cfg_} {
      o.cfg_ = nullptr;
   }

   ~cs_control() {
      if (!cfg_) {
         return;
      }

      // We don't want to throw from destructor
      std::error_code ec{error::success};
      pin::set(cfg_->cs_pin, false, ec);
      if (ec) {
         LOG_WRN("CS control error: %d", ec.value());
      }
   }

public:
   cs_control &operator=(const cs_control &) = delete;

   cs_control &operator=(cs_control &&o) {
      cfg_ = o.cfg_;
      o.cfg_ = nullptr;
      return *this;
   }

public:
   static cs_control take(const device &dev) {
      std::error_code ec{error::success};
      auto res = take(dev, ec);
      if (ec) {
         throw std::system_error{ec};
      }

      return std::move(*res);
   }

   static std::optional<cs_control> take(const device &dev, std::error_code &ec) {
      auto &cfg = get_config(dev);
      wait_for_ready_state(dev, ec);
      if (ec) {
         return {};
      }

      pin::set(cfg.cs_pin, true, ec);
      if (ec) {
         return {};
      }

      return cs_control(cfg);
   }

private:
   const it8951_config_t *cfg_;
};

//! Write a single word (16 bits), assuming the CS line is held.
void write_word(const device &dev, std::uint16_t word, std::error_code &ec) {
   std::array data{u8((word >> 8U) & 0xFF), u8(word & 0xFF)};
   const spi_buf tx_buf = {
      .buf = data.data(),
      .len = data.size(),
   };

   const struct spi_buf_set tx_buf_set = {
      .buffers = &tx_buf,
      .count = 1,
   };

   spi::write(get_config(dev).spi, tx_buf_set, ec);
}

//! Write some data assuming the CS line is held.
void write(const device &dev, const_span_t data, std::error_code &ec) {
   for (const auto word : data) {
      wait_for_ready_state(dev, ec);
      if (ec) {
         break;
      }

      write_word(dev, word, ec);
      if (ec) {
         break;
      }
   }
}

//! Read a single word (16 bits), assuming the CS line is held.
std::optional<std::uint16_t> read_word(const device &dev, std::error_code &ec) {
   std::array<std::uint8_t, 2> data{};
   const spi_buf rx_buf = {
      .buf = data.data(),
      .len = data.size(),
   };

   const struct spi_buf_set rx_buf_set = {
      .buffers = &rx_buf,
      .count = 1,
   };

   spi::read(get_config(dev).spi, rx_buf_set, ec);
   if (ec) {
      return {};
   }

   return u16(data[0]) << 8U | u16(data[1]);
}

// Read some data, assuming the CS line is already held down
// @note The first word is always discarded, as it will always contain garbage (junk from transferring the read request)
void read(const device &dev, span_t data, std::error_code &ec) {
   auto discard_first_word = [&]() {
      wait_for_ready_state(dev, ec);
      if (ec) {
         return;
      }

      (void)read_word(dev, ec);
      if (ec) {
         return;
      }
   };
   discard_first_word();

   for (auto &word : data) {
      wait_for_ready_state(dev, ec);
      if (ec) {
         return;
      }

      auto opt_word = read_word(dev, ec);
      if (ec) {
         return;
      }
      word = *opt_word;
   }
}

void set_image_buffer_base_address(const device &dev, std::error_code &ec) {
   const auto &data = get_data(dev);
   const auto address = data.info.image_buffer_address;

   const auto high = u16((address >> 16U) & 0xFFFF);
   const auto low = u16(address & 0xFFFF);

   hal::write_register(dev, hal::reg::lisar_high, high, ec);
   if (ec) {
      return;
   }

   hal::write_register(dev, hal::reg::lisar_low, low, ec);
   if (ec) {
      return;
   }
}

void load_image_area_start(const device &dev,
                           const common::image::area &area,
                           const common::image::config &config,
                           std::error_code &ec) {
   std::array args{
      u16((u16(config.endianness) << 8) | (u16(config.pixel_format) << 4) | u16(config.rotation)),
      u16(area.x),
      u16(area.y),
      u16(area.width),
      u16(area.height),
   };

   hal::write_command(dev, hal::command::load_image_area, args, ec);
}

void load_image_end(const device &dev, std::error_code &ec) {
   hal::write_command(dev, hal::command::load_image_end, ec);
}

void display_area(const device &dev, const common::image::area &area, common::waveform_mode mode, std::error_code &ec) {
   std::array args{
      u16(area.x), u16(area.y), u16(area.width), u16(area.height), u16(mode),
   };
   hal::write_command(dev, hal::command::display_area, args, ec);
}

} // namespace

namespace it8951::hal {

void write_command(const device &dev, command cmd, std::error_code &ec) {
   auto cs = cs_control::take(dev, ec);
   if (ec) {
      return;
   }

   write(dev, {{u16(preamble::write_command), u16(cmd)}}, ec);
}

void write_command(const device &dev, command cmd) {
   std::error_code ec{error::success};
   write_command(dev, cmd, ec);
   if (ec) {
      throw std::system_error{ec};
   }
}

void write_command(const device &dev, command cmd, const_span_t args, std::error_code &ec) {
   write_command(dev, cmd, ec);
   if (ec) {
      return;
   }

   for (auto word : args) {
      write_data(dev, {{word}}, ec);
      if (ec) {
         return;
      }
   }
}

void write_command(const device &dev, command cmd, const_span_t args) {
   std::error_code ec{error::success};
   write_command(dev, cmd, args, ec);
   if (ec) {
      throw std::system_error{ec};
   }
}

void write_data(const device &dev, const_span_t data, std::error_code &ec) {
   auto cs = cs_control::take(dev, ec);
   if (ec) {
      return;
   }

   write(dev, {{u16(preamble::write_data)}}, ec);
   if (ec) {
      return;
   }

   write(dev, data, ec);
}

void write_data(const device &dev, const_span_t data) {
   std::error_code ec{error::success};
   write_data(dev, data, ec);
   if (ec) {
      throw std::system_error{ec};
   }
}

void burst_write_one_chunk(const device &dev, std::span<const std::uint8_t> data, std::error_code &ec) {
   if (data.size() > CONFIG_EPD_BURST_WRITE_BUFFER_SIZE) {
      ec = error::invalid_usage;
      return;
   }

   std::uint16_t preamble = encoding::byte_swap(u16(preamble::write_data));

   // note: we are casting the const-ness away, but the SPI driver won't touch the
   // data anyway, so it should be fine
   auto ptr = const_cast<std::uint8_t *>(data.data());

   std::array spi_buffers{
      spi_buf{.buf = &preamble, .len = sizeof(preamble)},
      spi_buf{.buf = ptr, .len = static_cast<std::size_t>(data.size())},
   };

   const struct spi_buf_set tx_buf_set = {
      .buffers = spi_buffers.data(),
      .count = spi_buffers.size(),
   };

   auto cs = cs_control::take(dev, ec);
   if (ec) {
      return;
   }

   spi::write(get_config(dev).spi, tx_buf_set, ec);
}

void burst_write_one_chunk(const device &dev, std::span<const std::uint8_t> data) {
   std::error_code ec{error::success};
   burst_write_one_chunk(dev, data, ec);
   if (ec) {
      throw std::system_error{ec};
   }
}

void write_data_chunked_bursts(const device &dev, std::span<const std::uint8_t> data, std::error_code &ec) {
   for (std::size_t i = 0; i < data.size(); i += CONFIG_EPD_BURST_WRITE_BUFFER_SIZE) {
      const auto remainder = std::min<std::size_t>(data.size() - i, CONFIG_EPD_BURST_WRITE_BUFFER_SIZE);
      burst_write_one_chunk(dev, data.subspan(i, remainder), ec);
      if (ec) {
         return;
      }
   }
}

void write_data_chunked_bursts(const device &dev, std::span<const std::uint8_t> data) {
   std::error_code ec{error::success};
   write_data_chunked_bursts(dev, data, ec);
   if (ec) {
      throw std::system_error{ec};
   }
}

void write_register(const device &dev, reg reg, std::uint16_t value, std::error_code &ec) {
   write_command(dev, command::register_write, {{u16(reg), value}}, ec);
}

void write_register(const device &dev, reg reg, std::uint16_t value) {
   std::error_code ec{error::success};
   write_register(dev, reg, value, ec);
   if (ec) {
      throw std::system_error{ec};
   }
}

void read_data(const device &dev, span_t data, std::error_code &ec) {
   auto cs = cs_control::take(dev, ec);
   if (ec) {
      return;
   }

   write(dev, {{u16(preamble::read_data)}}, ec);
   if (ec) {
      return;
   }

   read(dev, data, ec);
}

void read_data(const device &dev, span_t data) {
   std::error_code ec{error::success};
   read_data(dev, data, ec);
   if (ec) {
      throw std::system_error{ec};
   }
}

std::optional<std::uint16_t> read_register(const device &dev, reg reg, std::error_code &ec) {
   write_command(dev, command::register_read, {{u16(reg)}}, ec);
   if (ec) {
      return {};
   }

   std::array<std::uint16_t, 1> result{};
   read_data(dev, result, ec);
   if (ec) {
      return {};
   }

   return result[0];
}

std::uint16_t read_register(const device &dev, reg reg) {
   std::error_code ec{error::success};
   auto res = read_register(dev, reg, ec);
   if (ec) {
      throw std::system_error{ec};
   }
   return *res;
}

void enable_packed_mode(const device &dev, std::error_code &ec) {
   write_register(dev, reg::i80cpcr, 0x0001, ec);
}

void enable_packed_mode(const device &dev) {
   std::error_code ec{error::success};
   enable_packed_mode(dev, ec);
   if (ec) {
      throw std::system_error{ec};
   }
}

namespace vcom {

std::optional<std::uint16_t> get(const device &dev, std::error_code &ec) {
   // Pass 0 as the only parameter to GET the VCOM value
   write_command(dev, command::set_vcom, {{u16(0)}}, ec);
   if (ec) {
      return {};
   }

   std::array<std::uint16_t, 1> result{};
   read_data(dev, result, ec);
   if (ec) {
      return {};
   }

   return result[0];
}

std::uint16_t get(const device &dev) {
   std::error_code ec{error::success};
   auto res = get(dev, ec);
   if (ec) {
      throw std::system_error{ec};
   }
   return *res;
}

void set(const device &dev, std::uint16_t value, std::error_code &ec) {
   // Pass 1 as the only parameter to SET the VCOM value
   write_command(dev, command::set_vcom, {{u16(1), value}}, ec);
}

void set(const device &dev, std::uint16_t value) {
   std::error_code ec{error::success};
   set(dev, value, ec);
   if (ec) {
      throw std::system_error{ec};
   }
}

} // namespace vcom

namespace system {

void run(const device &dev, std::error_code &ec) {
   write_command(dev, command::run, ec);
}

void run(const device &dev) {
   std::error_code ec{error::success};
   run(dev, ec);
   if (ec) {
      throw std::system_error{ec};
   }
}

void sleep(const device &dev, std::error_code &ec) {
   write_command(dev, command::sleep, ec);
}

void sleep(const device &dev) {
   std::error_code ec{error::success};
   sleep(dev, ec);
   if (ec) {
      throw std::system_error{ec};
   }
}

void power(const device &dev, bool is_on, std::error_code &ec) {
   write_command(dev, command::epd_power, {{u16(is_on ? 1 : 0)}}, ec);
}

void power(const device &dev, bool is_on) {
   std::error_code ec{error::success};
   power(dev, is_on, ec);
   if (ec) {
      throw std::system_error{ec};
   }
}

} // namespace system

namespace image {

void begin(const device &dev,
           const common::image::area &area,
           const common::image::config &config,
           std::error_code &ec) {
   system::power(dev, true, ec);
   if (ec) {
      return;
   }

   system::run(dev, ec);
   if (ec) {
      return;
   }

   wait_for_display_ready(dev, ec);
   if (ec) {
      return;
   }

   hal::enable_packed_mode(dev, ec);
   if (ec) {
      return;
   }

   set_image_buffer_base_address(dev, ec);
   if (ec) {
      return;
   }

   load_image_area_start(dev, area, config, ec);
}

void begin(const device &dev, const common::image::area &area, const common::image::config &config) {
   std::error_code ec{error::success};
   begin(dev, area, config, ec);
   if (ec) {
      throw std::system_error{ec};
   }
}

void end(const device &dev, const common::image::area &area, const common::waveform_mode mode, std::error_code &ec) {
   load_image_end(dev, ec);
   if (ec) {
      return;
   }

   display_area(dev, area, mode, ec);
}

void end(const device &dev, const common::image::area &area, const common::waveform_mode mode) {
   std::error_code ec{error::success};
   end(dev, area, mode, ec);
   if (ec) {
      throw std::system_error{ec};
   }
}

} // namespace image

} // namespace it8951::hal
