/**
 * @file   hal.cpp
 * @author Dennis Sitelew
 * @date   Jul. 28, 2024
 */

#include <it8951/hal.hpp>
#include <it8951/util.hpp>

#include <zephyr-cpp/drivers/gpio.hpp>
#include <zephyr-cpp/drivers/spi.hpp>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <optional>

#include <inttypes.h>

LOG_MODULE_REGISTER(it8951_hal, CONFIG_IT8951_LOG_LEVEL);

namespace {

using namespace it8951;
using namespace zephyr;

template <typename T>
inline auto u16(T v) {
   return static_cast<std::uint16_t>(v);
}

template <typename T>
inline auto u8(T v) {
   return static_cast<std::uint8_t>(v);
}

void_t wait_for_ready_state(const struct device &dev, k_timeout_t timeout = K_MSEC(CONFIG_EPD_READY_LINE_TIMEOUT)) {
   auto &data = get_data(dev);
   const uint32_t events = k_event_wait(&data.state, it8951_ready, false, timeout);
   if ((events & it8951_ready) == 0) {
      LOG_WRN("Ready state timeout");
      return unexpected(EBUSY);
   }
   return {};
}

void_t wait_for_display_ready(const struct device &dev,
                              k_timeout_t timeout = K_MSEC(CONFIG_EPD_DISPLAY_READY_TIMEOUT)) {
   const auto deadline = k_uptime_ticks() + timeout.ticks;
   while (k_uptime_ticks() < deadline) {
      auto read_res = hal::read_register(dev, hal::reg::lutafsr);
      if (!read_res) {
         return tl::unexpected{read_res.error()};
      }

      if (*read_res == 0) {
         // We are done waiting
         return {};
      }

      k_sleep(K_MSEC(1));
   }

   LOG_WRN("Display ready timeout");
   return unexpected(EBUSY);
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

      gpio::set(cfg_->cs_pin, false).or_else([](const auto &ec) {
         LOG_WRN("CS control error: %s", ec.message().c_str());
      });
   }

public:
   cs_control &operator=(const cs_control &) = delete;

   cs_control &operator=(cs_control &&o) {
      cfg_ = o.cfg_;
      o.cfg_ = nullptr;
      return *this;
   }

public:
   static expected<cs_control> take(const device &dev) {
      auto &cfg = get_config(dev);
      return wait_for_ready_state(dev)
         .and_then([&] {
            return gpio::set(cfg.cs_pin, true);
         })
         .and_then([&]() -> expected<cs_control> {
            return cs_control{cfg};
         });
   }

private:
   const it8951_config_t *cfg_;
};

//! Write a single word (16 bits), assuming the CS line is held.
void_t write_word(const device &dev, std::uint16_t word) {
   std::array data{u8((word >> 8U) & 0xFF), u8(word & 0xFF)};
   const spi_buf tx_buf = {
      .buf = data.data(),
      .len = data.size(),
   };

   const struct spi_buf_set tx_buf_set = {
      .buffers = &tx_buf,
      .count = 1,
   };

   return spi::write(get_config(dev).spi, tx_buf_set);
}

//! Write some data assuming the CS line is held.
void_t write(const device &dev, const_span_t data) {
   for (const auto word : data) {
      auto res = wait_for_ready_state(dev).and_then([&] {
         return write_word(dev, word);
      });

      if (!res) {
         return res;
      }
   }
   return {};
}

//! Read a single word (16 bits), assuming the CS line is held.
expected<std::uint16_t> read_word(const device &dev) {
   std::array<std::uint8_t, 2> data{};
   const spi_buf rx_buf = {
      .buf = data.data(),
      .len = data.size(),
   };

   const struct spi_buf_set rx_buf_set = {
      .buffers = &rx_buf,
      .count = 1,
   };

   return spi::read(get_config(dev).spi, rx_buf_set).and_then([&]() -> expected<std::uint16_t> {
      return u16(data[0]) << 8U | u16(data[1]);
   });
}

// Read some data, assuming the CS line is already held down
// @note The first word is always discarded, as it will always contain garbage (junk from transferring the read request)
void_t read(const device &dev, span_t data) {
   auto discard_first_word = [&]() {
      return wait_for_ready_state(dev).and_then([&] {
         return read_word(dev);
      });
   };
   return discard_first_word().and_then([&](auto _) -> void_t {
      for (auto &word : data) {
         auto res = wait_for_ready_state(dev)
                       .and_then([&] {
                          return read_word(dev);
                       })
                       .and_then([&](auto value) -> void_t {
                          word = value;
                          return {};
                       });

         if (!res) {
            return res;
         }
      }
      return {};
   });
}

void_t set_image_buffer_base_address(const device &dev) {
   const auto &data = get_data(dev);
   const auto address = data.info.image_buffer_address;

   const auto high = u16((address >> 16U) & 0xFFFF);
   const auto low = u16(address & 0xFFFF);

   return hal::write_register(dev, hal::reg::lisar_high, high).and_then([&] {
      return hal::write_register(dev, hal::reg::lisar_low, low);
   });
}

void_t load_image_area_start(const device &dev, const common::image::area &area, const common::image::config &config) {
   std::array args{
      u16((u16(config.endianness) << 8) | (u16(config.pixel_format) << 4) | u16(config.rotation)),
      u16(area.x),
      u16(area.y),
      u16(area.width),
      u16(area.height),
   };

   return hal::write_command(dev, hal::command::load_image_area, args);
}

void_t load_image_end(const device &dev) {
   return hal::write_command(dev, hal::command::load_image_end);
}

void_t display_area(const device &dev, const common::image::area &area, common::waveform_mode mode) {
   std::array args{
      u16(area.x), u16(area.y), u16(area.width), u16(area.height), u16(mode),
   };
   return hal::write_command(dev, hal::command::display_area, args);
}

} // namespace

namespace it8951::hal {

void_t write_command(const device &dev, command cmd) {
   return cs_control::take(dev).and_then([&](auto cs) {
      return write(dev, {{u16(preamble::write_command), u16(cmd)}});
   });
}

void_t write_command(const device &dev, command cmd, const_span_t args) {
   return write_command(dev, cmd).and_then([&]() -> void_t {
      for (auto word : args) {
         auto res = write_data(dev, {{word}});
         if (!res) {
            return res;
         }
      }
      return {};
   });
}

void_t write_data(const device &dev, const_span_t data) {
   return cs_control::take(dev).and_then([&](auto cs) {
      return write(dev, {{u16(preamble::write_data)}}).and_then([&] {
         return write(dev, data);
      });
   });
}

void_t burst_write_one_chunk(const device &dev, std::span<const std::uint8_t> data) {
   if (data.size() > CONFIG_EPD_BURST_WRITE_BUFFER_SIZE) {
      LOG_ERR("Bad write one chunk size: %d", (int)data.size());
      return unexpected(EINVAL);
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

   return cs_control::take(dev).and_then([&](auto cs) {
      return spi::write(get_config(dev).spi, tx_buf_set);
   });
}

void_t write_data_chunked_bursts(const device &dev, std::span<const std::uint8_t> data) {
   for (std::size_t i = 0; i < data.size(); i += CONFIG_EPD_BURST_WRITE_BUFFER_SIZE) {
      const auto remainder = std::min<std::size_t>(data.size() - i, CONFIG_EPD_BURST_WRITE_BUFFER_SIZE);
      auto res = burst_write_one_chunk(dev, data.subspan(i, remainder));
      if (!res) {
         return res;
      }
   }
   return {};
}

void_t write_register(const device &dev, reg reg, std::uint16_t value) {
   return write_command(dev, command::register_write, {{u16(reg), value}});
}

void_t read_data(const device &dev, span_t data) {
   return cs_control::take(dev).and_then([&](auto cs) {
      return write(dev, {{u16(preamble::read_data)}}).and_then([&] {
         return read(dev, data);
      });
   });
}

expected<std::uint16_t> read_register(const device &dev, reg reg) {
   return write_command(dev, command::register_read, {{u16(reg)}}).and_then([&] {
      std::array<std::uint16_t, 1> result{};
      return read_data(dev, result).and_then([&]() -> expected<std::uint16_t> {
         return result[0];
      });
   });
}

void_t enable_packed_mode(const device &dev) {
   return write_register(dev, reg::i80cpcr, 0x0001);
}

namespace vcom {

expected<std::uint16_t> get(const device &dev) {
   // Pass 0 as the only parameter to GET the VCOM value
   return write_command(dev, command::set_vcom, {{u16(0)}}).and_then([&] {
      std::array<std::uint16_t, 1> result{};
      return read_data(dev, result).and_then([&]() -> expected<std::uint16_t> {
         return result[0];
      });
   });
}

void_t set(const device &dev, std::uint16_t value) {
   // Pass 1 as the only parameter to SET the VCOM value
   return write_command(dev, command::set_vcom, {{u16(1), value}});
}

} // namespace vcom

namespace system {

void_t run(const device &dev) {
   return write_command(dev, command::run);
}

void_t sleep(const device &dev) {
   return write_command(dev, command::sleep);
}

void_t power(const device &dev, bool is_on) {
#define DEPRECATE_POWER 1
#ifdef DEPRECATE_POWER
   ARG_UNUSED(dev);
   ARG_UNUSED(is_on);

   LOG_ERR("You should not use the power function directly, it might fry your board!");
   return unexpected(EINVAL);
#else
   return write_command(dev, command::epd_power, {{u16(is_on ? 1 : 0)}});
#endif
}

} // namespace system

namespace image {

void_t begin(const device &dev, const common::image::area &area, const common::image::config &config) {
   return system::run(dev)
      .and_then([&] {
         return wait_for_display_ready(dev);
      })
      .and_then([&] {
         return enable_packed_mode(dev);
      })
      .and_then([&] {
         return set_image_buffer_base_address(dev);
      })
      .and_then([&] {
         return load_image_area_start(dev, area, config);
      });
}

void_t end(const device &dev, const common::image::area &area, const common::waveform_mode mode) {
   return load_image_end(dev)
      .and_then([&] {
         return display_area(dev, area, mode);
      })
      .and_then([&] {
         // Make sure we exit this function with the display in the ready state this way we can be sure that the
         // panel has finished rendering the new image.
         return wait_for_ready_state(dev);
      })
      .and_then([&] {
         // Afterward we put the driver board into sleep mode and again wait until it is ready. This way we can avoid
         // a potential burn-out of the driver board itself.
         return system::sleep(dev);
      })
      .and_then([&] {
         return wait_for_ready_state(dev);
      });
}

} // namespace image

} // namespace it8951::hal
