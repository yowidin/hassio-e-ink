/**
 * @file   init.cpp
 * @author Dennis Sitelew
 * @date   Jul. 25, 2024
 */

#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>

#include <zephyr-cpp/drivers/gpio.hpp>
#include <zephyr-cpp/drivers/spi.hpp>

#include <it8951/hal.hpp>
#include <it8951/util.hpp>

#include <it8951/init.h>

LOG_MODULE_REGISTER(it8951, CONFIG_IT8951_LOG_LEVEL);

using namespace it8951;

using namespace zephyr;

namespace {

void_t check_and_init_input_pin(const struct gpio_dt_spec &spec) {
   return gpio::ready(spec).and_then([&] {
      return gpio::configure(spec, GPIO_INPUT);
   });
}

void_t check_and_init_output_pin(const struct gpio_dt_spec &spec, bool initial_state) {
   return gpio::ready(spec)
      .and_then([&] {
         return gpio::configure(spec, GPIO_OUTPUT);
      })
      .and_then([&] {
         return gpio::set(spec, initial_state);
      });
}

void_t update_ready_state(const struct device &dev) {
   const auto &cfg = it8951::get_config(dev);
   auto &data = get_data(dev);

   return gpio::get(cfg.ready_pin)
      .and_then([&](auto is_high) -> void_t {
         if (is_high) {
            k_event_post(&data.state, it8951_ready);
         } else {
            k_event_clear(&data.state, it8951_ready);
         }
         return {};
      })
      .or_else([&](const auto &err) -> void_t {
         LOG_ERR("Error getting CS state: %s", err.message().c_str());
         k_event_post(&data.state, it8951_error);
         return tl::unexpected{err};
      });
}

void on_ready_interrupt(const struct device *gpio, struct gpio_callback *cb, uint32_t pins) {
   ARG_UNUSED(gpio);
   ARG_UNUSED(pins);

   auto data = CONTAINER_OF(cb, it8951_data_t, ready_cb);
   (void)update_ready_state(*data->dev);
}

void_t setup_ready_pin(const struct device &dev) {
   const auto &cfg = get_config(dev);
   auto &data = get_data(dev);

   k_event_init(&data.state);

   // Setup READY pin
   return check_and_init_input_pin(cfg.ready_pin)
      .and_then([&] {
         gpio_init_callback(&data.ready_cb, on_ready_interrupt, BIT(cfg.ready_pin.pin));
         return gpio::interrupt_configure(cfg.ready_pin, GPIO_INT_EDGE_BOTH);
      })
      .and_then([&] {
         return gpio::add_callback(cfg.ready_pin, data.ready_cb);
      })
      .and_then([&] {
         // Start with a known pin state
         return update_ready_state(dev);
      });
}

void_t reset(const it8951_config_t &cfg) {
   return gpio::set(cfg.reset_pin, false)
      .and_then([&] {
         k_sleep(K_MSEC(200));
         return gpio::set(cfg.reset_pin, true);
      })
      .and_then([&] {
         k_sleep(K_MSEC(10));
         return gpio::set(cfg.reset_pin, false);
      })
      .and_then([&]() -> expected<void> {
         k_sleep(K_MSEC(200));
         return {};
      });
}

void_t read_device_info(const struct device &dev, it8951_device_info_t &info) {
   return hal::write_command(dev, hal::command::get_device_info).and_then([&] {
      std::array<std::uint16_t, 20> rx_buffer = {};

      auto read_string = [&rx_buffer](int start, auto &target) {
         const auto begin = reinterpret_cast<const char *>(&rx_buffer[start]);
         const auto end = begin + 16;
         std::copy(begin, end, target);
      };

      return hal::read_data(dev, rx_buffer).and_then([&]() -> void_t {
         // LOG_HEXDUMP_ERR(rx_buffer.data(), rx_buffer.size(), "Info message");
         info.panel_width = rx_buffer[0];
         info.panel_height = rx_buffer[1];

         const std::uint32_t low = rx_buffer[2];
         const std::uint32_t high = rx_buffer[3];

         info.image_buffer_address = low | (high << 16);

         read_string(4, info.it8951_version); // Bytes 4-12: FW Version
         read_string(12, info.lut_version);   // Bytes 12-20: LUT Version

         return {};
      });
   });
}

expected<void> try_init(const struct device &dev) {
   const auto &cfg = get_config(dev);
   auto &data = get_data(dev);

   data.dev = &dev;

   return setup_ready_pin(dev)
      .and_then([&] {
         return check_and_init_output_pin(cfg.reset_pin, false);
      })
      .and_then([&] {
         return check_and_init_output_pin(cfg.cs_pin, false);
      })
      .and_then([&] {
         return zephyr::spi::ready(cfg.spi);
      })
      .and_then([&] {
         return reset(cfg);
      })
      .and_then([&] {
         return hal::system::run(dev);
      })
      .and_then([&] {
         return read_device_info(dev, data.info);
      })
      .and_then([&] {
         return hal::enable_packed_mode(dev);
      })
      .and_then([&] {
         return hal::vcom::get(dev);
      })
      .and_then([&](auto vcom) -> void_t {
         if (vcom != cfg.vcom) {
            LOG_INF("Updating VCOM value from %" PRIu16 " to %" PRIu16, vcom, cfg.vcom);
            return hal::vcom::set(dev, cfg.vcom);
         } else {
            return {};
         }
      })
      .and_then([&]() -> void_t {
         // Only at this point are we sure that we have a functioning board
         auto &info = data.info;
         LOG_DBG(
            "Display info:\r\n"
            "\tWidth  = %d\r\n"
            "\tHeight = %d\r\n"
            "\tBuffer Address: 0x%x\r\n"
            "\tFW Version: %s\r\n"
            "\tLUT Version: %s",
            info.panel_width, info.panel_height, info.image_buffer_address, info.it8951_version, info.lut_version);
         return {};
      });
}

} // namespace

extern "C" {

int it8951_init(const struct device *dev) {
   auto res = try_init(*dev);
   if (!res) {
      return -res.error().value();
   }

   return 0;
}

} // extern "C"