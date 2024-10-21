/**
 * @file   shutdown.cpp
 * @author Dennis Sitelew
 * @date   Oct. 20, 2024
 */

#include <hei/shutdown.hpp>

#include <zephyr/drivers/uart.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>

#include <zephyr/sys/crc.h>

#include <array>
#include <cstdint>
#include <utility>

LOG_MODULE_REGISTER(shutdown, CONFIG_APP_LOG_LEVEL);

namespace {

const device *uart_dev = DEVICE_DT_GET(DT_NODELABEL(uart2));

extern "C" {

int init_uart(void) {
   if (!device_is_ready(uart_dev)) {
      LOG_ERR("Shutdown UART not ready");
      return -ENODEV;
   }

   return 0;
}

} // extern "C"

} // namespace

void hei::shutdown::request(std::chrono::seconds duration) {
   const auto num_seconds = static_cast<std::uint16_t>(duration.count());

   LOG_INF("Shutting down for %d seconds", static_cast<int>(num_seconds));

   auto split = [](const std::uint16_t value) -> std::pair<std::uint8_t, std::uint8_t> {
      const auto low = static_cast<std::uint8_t>(value & 0xFF);
      const auto high = static_cast<std::uint8_t>((value >> 8) & 0xFF);
      return {low, high};
   };

   const auto seconds = split(num_seconds);
   std::array<std::uint8_t, 6> payload = {
      0xDE, 0xAD, seconds.first, seconds.second, 0xBE, 0xEF,
   };

   // 2 bytes magic number + 2 bytes duration = 4 bytes total length
   const auto crc_value = crc16_ansi(payload.data(), 4);
   const auto crc = split(crc_value);
   payload[4] = crc.first;
   payload[5] = crc.second;

   for (const auto byte : payload) {
      uart_poll_out(uart_dev, byte);
   }
}

SYS_INIT(init_uart, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
