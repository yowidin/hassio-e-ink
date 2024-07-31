/**
 * @file   driver.c
 * @author Dennis Sitelew
 * @date   Jul. 25, 2024
 */

#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>

#include <it8951/driver.hpp>
#include <it8951/error.hpp>
#include <it8951/hal.hpp>
#include <it8951/util.hpp>

#include <it8951/init.h>

LOG_MODULE_REGISTER(it8951, CONFIG_IT8951_LOG_LEVEL);

using namespace it8951;

namespace {

void check_and_init_input_pin(const struct gpio_dt_spec &spec) {
   pin::check(spec);
   pin::configure(spec, GPIO_INPUT);
}

void check_and_init_output_pin(const struct gpio_dt_spec &spec, bool initial_state) {
   pin::check(spec);
   pin::configure(spec, GPIO_OUTPUT);
   pin::set(spec, initial_state);
}

void update_ready_state(const struct device &dev) {
   const auto &cfg = it8951::get_config(dev);
   auto &data = get_data(dev);

   int res = gpio_pin_get_dt(&cfg.ready_pin);
   switch (res) {
      case 0:
         k_event_clear(&data.state, it8951_ready);
         break;

      case 1:
         k_event_post(&data.state, it8951_ready);
         break;

      default:
         LOG_ERR("Error getting CS state: %d", res);
         k_event_post(&data.state, it8951_error);
   }
}

void on_ready_interrupt(const struct device *gpio, struct gpio_callback *cb, uint32_t pins) {
   ARG_UNUSED(gpio);
   ARG_UNUSED(pins);

   auto data = CONTAINER_OF(cb, it8951_data_t, ready_cb);
   update_ready_state(*data->dev);
}

void setup_ready_pin(const struct device &dev) {
   const auto &cfg = get_config(dev);
   auto &data = get_data(dev);

   k_event_init(&data.state);

   // Setup READY pin
   check_and_init_input_pin(cfg.ready_pin);

   gpio_init_callback(&data.ready_cb, on_ready_interrupt, BIT(cfg.ready_pin.pin));

   it8951::pin::interrupt_configure(cfg.ready_pin, GPIO_INT_EDGE_BOTH);
   it8951::pin::add_callback(cfg.ready_pin, data.ready_cb);

   // Start with a known pin state
   update_ready_state(dev);
   if (k_event_test(&data.state, it8951_error)) {
      LOG_ERR("Ready pin read failed");
      throw std::system_error(error::hal_error);
   }
}

void reset(const it8951_config_t &cfg) {
   pin::set(cfg.reset_pin, true);
   k_sleep(K_MSEC(100));
   pin::set(cfg.reset_pin, false);
}

void read_device_info(const struct device &dev, it8951_device_info_t &info) {
   hal::write_command(dev, it8951::command_t::get_device_info);

   std::array<std::uint16_t, 20> rx_buffer = {};
   hal::read_data(dev, rx_buffer);

   LOG_HEXDUMP_DBG(rx_buffer.data(), rx_buffer.size() * 2, "Info");

   info.panel_width = rx_buffer[0];
   info.panel_height = rx_buffer[1];

   const std::uint32_t low = rx_buffer[2];
   const std::uint32_t high = rx_buffer[3];
   info.image_buffer_address = low | (high << 16);

   auto read_string = [&rx_buffer](int start, auto &target) {
      const auto begin = reinterpret_cast<const char *>(&rx_buffer[start]);
      const auto end = begin + 16;
      std::copy(begin, end, target);
   };

   read_string(4, info.it8951_version); // Bytes 4-12: FW Version
   read_string(12, info.lut_version);   // Bytes 12-20: LUT Version
}

void try_init(const struct device &dev) {
   const auto &cfg = get_config(dev);
   auto &data = get_data(dev);

   data.dev = &dev;

   setup_ready_pin(dev);
   check_and_init_output_pin(cfg.reset_pin, false);
   check_and_init_output_pin(cfg.cs_pin, false);

   if (!spi_is_ready_dt(&cfg.spi)) {
      LOG_WRN("EPD SPI not ready");
      throw std::system_error(error::hal_error);
   }

   reset(cfg);

   auto &info = data.info;
   read_device_info(dev, info);

   hal::enable_packed_mode(dev);

   auto vcom = hal::vcom::get(dev);
   if (vcom != cfg.vcom) {
      LOG_INF("Updating VCOM value from %" PRIu16 " to %" PRIu16, vcom, cfg.vcom);
      hal::vcom::set(dev, cfg.vcom);
   }

   // Only at this point are we sure that we have a functioning board
   LOG_DBG(
      "Display info:\r\n"
      "\tWidth  = %d\r\n"
      "\tHeight = %d\r\n"
      "\tBuffer Address: 0x%x\r\n"
      "\tFW Version: %s\r\n"
      "\tLUT Version: %s",
      info.panel_width, info.panel_height, info.image_buffer_address, info.it8951_version, info.lut_version);
}

} // namespace

extern "C" {

int it8951_init(const struct device *dev) {
   try {
      try_init(*dev);
   } catch (const std::exception &e) {
      LOG_ERR("IT8951 initialization error: %s", e.what());
      return -ENODEV;
   }
   return 0;
}

} // extern "C"