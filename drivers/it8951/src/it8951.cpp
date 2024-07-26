/**
 * @file   it8951.cpp
 * @author Dennis Sitelew
 * @date   Jul. 25, 2024
 */

#include <it8951/driver.h>
#include <it8951/it8951.hpp>

#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <array>
#include <cstdint>
#include <span>

LOG_MODULE_DECLARE(it8951, CONFIG_IT8951_LOG_LEVEL);

#if CONFIG_LITTLE_ENDIAN
#define WORD_SWAP(x) static_cast<std::uint16_t>(((x & 0xFF00) >> 8) | ((x & 0x00FF) << 8))
#else
#define WORD_SWAP(x) (x)
#endif

namespace {

class cs_control {
public:
   cs_control(const it8951_config_t *cfg)
      : cfg_{cfg} {
      if (int err = gpio_pin_set_dt(&cfg_->cs_pin, 1)) {
         LOG_WRN("CS pin set error: %d", err);
         return;
      }
   }

   ~cs_control() {
      if (int err = gpio_pin_set_dt(&cfg_->cs_pin, 0)) {
         LOG_WRN("CS pin set error: %d", err);
         return;
      }
   }

private:
   const it8951_config_t *cfg_;
};

void wait_for_ready(const it8951_config_t *cfg) {
   auto start = k_uptime_get();

   int res = gpio_pin_get_dt(&cfg->ready_pin);
   while (res != 1) {
      k_sleep(K_USEC(100));
      if (k_uptime_get() - start > 10 * MSEC_PER_SEC) {
         break;
      }

      res = gpio_pin_get_dt(&cfg->ready_pin);
   }
}

void reset_device(const it8951_config_t *cfg) {
   LOG_DBG("Resetting");

   if (int err = gpio_pin_set_dt(&cfg->reset_pin, 1)) {
      LOG_ERR("Reset pin set error: %d", err);
      return;
   }
   k_sleep(K_MSEC(10));

   if (int err = gpio_pin_set_dt(&cfg->reset_pin, 0)) {
      LOG_ERR("Reset pin set error: %d", err);
      return;
   }

   LOG_DBG("Done resetting");
}

template <typename T>
spi_buf make_buf(T &container) {
   return spi_buf{
       .buf = std::data(container),
       .len = std::size(container) * sizeof(typename T::value_type),
   };
}

bool write_command(const it8951_config_t *cfg, std::uint16_t command) {
   const auto COMMAND_PREAMBLE = WORD_SWAP(0x6000);

   std::array command_buffer = {COMMAND_PREAMBLE, command};
   const auto spi_buf = make_buf(command_buffer);
   const struct spi_buf_set tx_buffers = {
       .buffers = &spi_buf,
       .count = 1,
   };

   wait_for_ready(cfg);

   cs_control ctrl{cfg};

   int err = spi_write_dt(&cfg->spi, &tx_buffers);
   if (err != 0) {
      LOG_ERR("SPI write failed, %s", strerror(errno));
      return false;
   }

   return true;
}

template <std::size_t Size>
bool read_data(const it8951_config_t *cfg, std::array<std::uint16_t, Size> &data) {
   const auto READ_PREAMBLE = WORD_SWAP(0x1000);

   std::array command_buffer = {READ_PREAMBLE};
   const auto tx_buf = make_buf(command_buffer);
   const struct spi_buf_set tx = {
       .buffers = &tx_buf,
       .count = 1,
   };

   wait_for_ready(cfg);

   cs_control ctrl{cfg};

   int err = spi_write_dt(&cfg->spi, &tx);
   if (err != 0) {
      LOG_ERR("SPI write failed, %s", strerror(errno));
      return false;
   }

   auto read_word = [&](std::size_t offset) {
      const struct spi_buf rx_buf = {
          .buf = static_cast<void *>((&data[offset])),
          .len = sizeof(std::uint16_t),
      };
      const struct spi_buf_set rx = {
          .buffers = &rx_buf,
          .count = 1,
      };

      wait_for_ready(cfg);

      int err = spi_read_dt(&cfg->spi, &rx);
      if (err != 0) {
         LOG_ERR("SPI read failed, %s", strerror(errno));
         return false;
      }

      LOG_INF("Read: %x", data[offset]);
      return true;
   };

   // Skip one dummy word
   if (!read_word(0)) {
      return false;
   }

   // Read the actual data
   for (std::size_t i = 0; i < data.size(); ++i) {
      if (!read_word(i)) {
         return false;
      }
   }

   return true;
}

} // namespace

namespace it8951 {

void read_device_info(const device *dev) {
   const auto cfg = reinterpret_cast<const it8951_config_t *>(dev->config);

   reset_device(cfg);

   const auto GET_DEVICE_INFO = WORD_SWAP(0x0302);
   write_command(cfg, GET_DEVICE_INFO);

   std::array<std::uint16_t, 20> rx_buffer = {};
   read_data(cfg, rx_buffer);

   LOG_HEXDUMP_INF(rx_buffer.data(), rx_buffer.size() * sizeof(uint16_t), "RX Buffer");
}

} // namespace it8951
