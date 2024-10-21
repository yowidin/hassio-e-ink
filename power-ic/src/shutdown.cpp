/**
 * @file   shutdown.cpp
 * @author Dennis Sitelew
 * @date   Oct. 20, 2024
 */

#include <power-ic/shutdown.hpp>

#include <autoconf.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>
#include <zephyr/sys/crc.h>

#include <inttypes.h>

#include <array>

LOG_MODULE_REGISTER(shutdown, CONFIG_APP_LOG_LEVEL);

namespace {

const device *uart_dev = DEVICE_DT_GET(DT_NODELABEL(uart0));

extern "C" {

int init_uart(void) {
   if (!device_is_ready(uart_dev)) {
      LOG_ERR("Shutdown UART not ready");
      return -ENODEV;
   }

   return 0;
}

} // extern "C"

class shutdown_request {
public:
   enum class state {
      first_magic,
      second_magic,
      low_duration,
      high_duration,
      low_crc,
      high_crc,
      done,
   };

public:
   state add_byte(std::uint8_t byte) {
      switch (state_) {
         case state::first_magic:
            return on_first_magic(byte);
         case state::second_magic:
            return on_second_magic(byte);
         case state::low_duration:
            return on_low_duration(byte);
         case state::high_duration:
            return on_high_duration(byte);
         case state::low_crc:
            return on_low_crc(byte);
         case state::high_crc:
            return on_high_crc(byte);
         case state::done:
            return on_done(byte);
         default:
            return on_default(byte);
      }
   }

   std::chrono::seconds sleep_duration() {
      if (state_ != state::done) {
         throw std::runtime_error("Invalid state");
      }

      const auto seconds = to_uint16_t(low_duration_, high_duration_);
      return std::chrono::seconds{seconds};
   }

private:
   state on_first_magic(std::uint8_t byte) {
      if (byte == 0xDE) {
         state_ = state::second_magic;
      } else {
         log_bad_byte(byte);
      }
      return state_;
   }

   state on_second_magic(std::uint8_t byte) {
      if (byte == 0xAD) {
         state_ = state::low_duration;
      } else {
         log_bad_byte(byte);
      }
      return state_;
   }

   state on_low_duration(std::uint8_t byte) {
      low_duration_ = byte;
      state_ = state::high_duration;
      return state_;
   }

   state on_high_duration(std::uint8_t byte) {
      high_duration_ = byte;
      state_ = state::low_crc;
      return state_;
   }

   state on_low_crc(std::uint8_t byte) {
      low_crc_ = byte;
      state_ = state::high_crc;
      return state_;
   }

   state on_high_crc(std::uint8_t byte) {
      high_crc_ = byte;

      const std::array<std::uint8_t, 4> payload{0xDE, 0xAD, low_duration_, high_duration_};
      const auto received_crc = to_uint16_t(low_crc_, high_crc_);

      const auto crc_value = crc16_ansi(payload.data(), payload.size());
      if (received_crc != crc_value) {
         LOG_WRN("Checksum mismatch: %" PRIu16 " vs %" PRIu16, received_crc, crc_value);
         state_ = state::first_magic;
      } else {
         state_ = state::done;
      }

      return state_;
   }

   state on_done(std::uint8_t byte) {
      LOG_WRN("Implicit transition from 'done' to 'first magic byte'");
      return on_first_magic(byte);
   }

   state on_default(std::uint8_t byte) {
      ARG_UNUSED(byte);
      LOG_ERR("Unexpected state: %d", static_cast<int>(state_));
      state_ = state::first_magic;
      return state_;
   }

   void log_bad_byte(std::uint8_t byte) {
      LOG_WRN("%s, unexpected byte: %x", to_string(state_), static_cast<int>(byte));
      state_ = state::first_magic;
   }

   std::uint16_t to_uint16_t(std::uint8_t low, std::uint16_t high) {
      return static_cast<std::uint16_t>((high << 8) | low);
   }

   static const char *to_string(state s) {
      switch (s) {
         case state::first_magic:
            return "frist_magic";
         case state::second_magic:
            return "second_magic";
         case state::low_duration:
            return "low_duration";
         case state::high_duration:
            return "high_duration";
         case state::low_crc:
            return "low_crc";
         case state::high_crc:
            return "high_crc";
         case state::done:
            return "done";
         default:
            return "[unknown]";
      }
   }

private:
   state state_{state::first_magic};

   std::uint8_t low_duration_{};
   std::uint8_t high_duration_{};

   std::uint8_t low_crc_{};
   std::uint8_t high_crc_{};
};

} // namespace

namespace uart {

void suspend() {
   LOG_DBG("Suspending shutdown UART");

   const int err = pm_device_action_run(uart_dev, PM_DEVICE_ACTION_SUSPEND);
   if (err < 0 && err != -EALREADY) {
      LOG_ERR("Error suspending shutdown UART: %d", err);
   }
}

void resume() {
   LOG_DBG("Resuming shutdown UART");

   const int err = pm_device_action_run(uart_dev, PM_DEVICE_ACTION_RESUME);
   if (err < 0 && err != -EALREADY) {
      LOG_ERR("Error resuming shutdown UART: %d", err);
   }
}

} // namespace uart

std::chrono::seconds power_ic::shutdown::get_sleep_duration() {
   // While booting up, the ESP32 shortly toggles the UART pin, skip this by sleeping half a second
   k_sleep(K_MSEC(500));

   uart::resume();

   shutdown_request req;
   std::chrono::seconds result{};

   while (true) {
      std::uint8_t received;
      const auto res = uart_poll_in(uart_dev, &received);
      if (res == 0) {
         // Got a byte
         auto state = req.add_byte(received);
         if (state == shutdown_request::state::done) {
            result = req.sleep_duration();
            LOG_DBG("Received sleep request: %d seconds", static_cast<int>(result.count()));
            break;
         }
      } else if (res == -1) {
         // No character was available to read.
         k_sleep(K_MSEC(100));
         continue;
      } else {
         LOG_ERR("UART error: %d", res);

         // At least let the ESP32 chpip update the image (and return the default sleep interval)
         k_sleep(K_SECONDS(20));
         result = std::chrono::seconds{CONFIG_APP_WAKE_UP_INTERVAL};
         break;
      }
   }

   uart::suspend();

   return result;
}

SYS_INIT(init_uart, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
