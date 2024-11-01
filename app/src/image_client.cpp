/**
 * @file   image_client.cpp
 * @author Dennis Sitelew
 * @date   Sep. 30, 2024
 */

#include <hei/fuel_gauge.h>
#include <hei/common.hpp>
#include <hei/display.hpp>
#include <hei/image_client.hpp>
#include <hei/settings.hpp>
#include <hei/shutdown.hpp>

#include <zephyr-cpp/error.hpp>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <cerrno>

#include <lz4.h>

#include <array>
#include <tuple>
#include <utility>

#include <autoconf.h>

#if CONFIG_SHELL
#include <zephyr/shell/shell.h>
#endif

LOG_MODULE_REGISTER(image_client, CONFIG_APP_LOG_LEVEL);

using namespace zephyr;

namespace {

enum client_event_t {
   ce_start = BIT(0),
   ce_manual_fetch = BIT(1),
   ce_stop = BIT(2),
};

K_EVENT_DEFINE(client_events)

class image_client {
private:
   enum class message_type : std::uint8_t {
      get_image_request = 0x10,
      image_header_response = 0x11,
      image_block_response = 0x12,
      server_error = 0x50,
   };

   struct get_image_request {
   public:
      // type: u8, fg_valid: u8, runtime_to_empty: u32, runtime_to_full: u32, charge_percentage: u8, voltage: u32
      static constexpr std::size_t array_size = 1 + 1 + 4 + 4 + 1 + 4;
      using array_t = std::array<std::uint8_t, array_size>;

   public:
      get_image_request() {
         auto it = payload.begin();
         write(it, static_cast<std::uint8_t>(message_type::get_image_request));

         const auto fg = hei_fuel_gauge_get();
         write(it, static_cast<std::uint8_t>(fg.valid));
         if (!fg.valid) {
            // We don't care about rest of the fields if the fuel gauge is not available
            return;
         }

         write(it, fg.runtime_to_empty_minutes);
         write(it, fg.runtime_to_full_minutes);
         write(it, fg.relative_state_of_charge_percentage);
         write(it, fg.voltage_uv);
      }

   private:
      template <std::unsigned_integral T>
      void write(array_t::iterator &it, const T value) {
         constexpr auto size = sizeof(T);
         if (size == 1) {
            // Only one byte - just write it
            *it = static_cast<std::uint8_t>(value);
            std::advance(it, 1U);
            return;
         }

         // Encode as little-endian
         for (std::size_t i = 0; i < size; ++i) {
            if (i == 0) {
               *it = static_cast<std::uint8_t>(value & 0xFF);
            } else {
               *it = static_cast<std::uint8_t>((value >> (i * 8)) & 0xFF);
            }

            std::advance(it, 1U);
         }
      }

   public:
      array_t payload{};
   };

public:
   static void thread_fn(void *p1, void *p2, void *p3) {
      ARG_UNUSED(p2);
      ARG_UNUSED(p3);

      auto client = static_cast<image_client *>(p1);
      client->main();
   }

private:
   void main() {
      (void)k_event_wait(&client_events, ce_start, false, K_FOREVER);

      const auto interval_opt = hei::settings::image_server::refresh_interval();

      std::chrono::seconds sleep_duration{60};
      if (interval_opt) {
         sleep_duration = *interval_opt;
      }

      if (!convert_server_address()) {
         return;
      }

      while (true) {
         const auto start = k_uptime_get();

         auto res = fetch_image();
         if (res) {
            const auto end = k_uptime_get();
            const auto delta = end - start;
            LOG_INF("Received image in %" PRIi64 " ms", delta);
         } else {
            LOG_ERR("Image client error: %s", res.error().message().c_str());
         }

         if (socket_) {
            if (close(socket_)) {
               LOG_ERR("Close error: %s", strerror(errno));
            }

            socket_ = 0;
         }

         // Try shutting down
         for (int i = 0; i < 10; ++i) {
            hei::shutdown::request(sleep_duration);
            k_sleep(K_MSEC(100));
         }

         auto events = k_event_wait(&client_events, ce_manual_fetch | ce_stop, true, K_SECONDS(sleep_duration.count()));
         if (events & ce_stop) {
            k_event_wait(&client_events, ce_start, true, K_FOREVER);
         }
      }
   }

   static auto report_error(const char *message, int error) {
      LOG_ERR("%s: %s", message, strerror(error));
      return unexpected(error);
   }

   void_t fetch_image() {
      namespace common_t = it8951::common;

      if ((socket_ = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
         return report_error("Socket creation error", errno);
      }

      if (connect(socket_, reinterpret_cast<sockaddr *>(&server_address_), sizeof(server_address_)) < 0) {
         return report_error("Connection failed", errno);
      }

      LOG_INF("Connected to server");

      // Set socket to non-blocking mode
      int flags = fcntl(socket_, F_GETFL, 0);
      if (flags < 0) {
         return report_error("Error getting socket flags", errno);
      }

      if (fcntl(socket_, F_SETFL, flags | O_NONBLOCK) < 0) {
         return report_error("Error setting socket flags", errno);
      }

      // Request the new image (together with the refresh type) and send the fuel gauge readings at the same time
      get_image_request req{};
      if (auto res = send(req.payload); !res) {
         return report_error("Error sending request", res.error().value());
      }

      // Read header: message_type: u8, update_type: u8, width: u16, height: u16, num_blocks: u16
      using response_t = std::tuple<std::uint8_t, std::uint8_t, std::uint16_t, std::uint16_t, std::uint16_t>;
      auto response_res = read_tuple<response_t>();
      if (!response_res) {
         return tl::unexpected{response_res.error()};
      }

      const auto [type_raw, mode_raw, width_raw, image_height, num_blocks] = *response_res;

      auto type = static_cast<message_type>(type_raw);
      if (type == message_type::server_error) {
         LOG_WRN("Server error");
         return unexpected(EBADMSG);
      }

      if (type != message_type::image_header_response) {
         LOG_ERR("Bad response: %" PRIu8, static_cast<std::uint8_t>(type));
         return unexpected(EBADMSG);
      }

      const auto mode = static_cast<common_t::waveform_mode>(mode_raw);
      switch (mode) {
         case it8951::common::waveform_mode::init:
         case it8951::common::waveform_mode::direct_update:
         case it8951::common::waveform_mode::grayscale_clearing:
         case it8951::common::waveform_mode::grayscale_limited:
         case it8951::common::waveform_mode::grayscale_limited_reduced:
            break;

         default:
            LOG_ERR("Bad wave form mode: %d", static_cast<int>(mode));
            return unexpected(EBADMSG);
      }

      // x2 because the transmitted image is 4 bytes per pixel

      const auto image_width = static_cast<std::uint16_t>(width_raw * 2);

      auto &display = hei::display::get();
      auto dr = display.begin({.x = 0, .y = 0, .width = image_width, .height = image_height},
                              {.endianness = common_t::endianness::little,
                               .pixel_format = common_t::pixel_format::pf4bpp,
                               .rotation = common_t::rotation::rotate0,
                               .mode = mode});
      if (!dr) {
         return dr;
      }

      LOG_DBG("Image Header: w=%" PRIu16 ", h=%" PRIu16 ", n=%" PRIu16, image_width, image_height, num_blocks);
      for (std::uint16_t block = 0; block < num_blocks; ++block) {
         using block_t = std::tuple<std::uint8_t, std::uint16_t, std::uint16_t>;
         auto block_res = read_tuple<block_t>();
         if (!block_res) {
            return tl::unexpected{response_res.error()};
         }

         const auto [block_type_raw, uncompressed_size, compressed_size] = *block_res;
         type = static_cast<message_type>(block_type_raw);
         if (type == message_type::server_error) {
            LOG_WRN("Server error");
            return unexpected(EBADMSG);
         }

         if (type != message_type::image_block_response) {
            LOG_ERR("Bad block type: %" PRIu8, static_cast<std::uint8_t>(type));
            return unexpected(EBADMSG);
         }

         auto ec = receive(compressed_size);
         if (!ec) {
            return report_error("Error receiving block", ec.error().value());
         }

         const auto res = LZ4_decompress_safe(reinterpret_cast<const char *>(recv_buffer_.data()),
                                              reinterpret_cast<char *>(image_buffer_.data()), compressed_size,
                                              static_cast<int>(image_buffer_.size()));
         if (res < 0) {
            LOG_ERR("Image block decompression error: %d", res);
            return unexpected(res);
         }

         if (res != uncompressed_size) {
            LOG_ERR("Decompressed data size mismatch: %d vs %d", res, static_cast<int>(uncompressed_size));
            return unexpected(EBADMSG);
         }

         dr = display.update({image_buffer_.data(), uncompressed_size});
         if (!dr) {
            return dr;
         }
      }

      return display.end();
   }

   bool convert_server_address() {
      const auto port_opt = hei::settings::image_server::port();
      if (!port_opt) {
         LOG_ERR("Missing server port");
         return false;
      }

      const auto address_opt = hei::settings::image_server::address();
      if (!address_opt) {
         LOG_ERR("Missing server address");
         return false;
      }

      server_address_.sin_family = AF_INET;
      server_address_.sin_port = htons(*port_opt);

      // TODO: DNS support
      if (inet_pton(AF_INET, address_opt->data(), &server_address_.sin_addr) <= 0) {
         LOG_ERR("Unsupported address: %s", address_opt->data());
         return false;
      }

      return true;
   }

   template <typename Tuple, std::size_t I>
   bool read_tuple_element(Tuple &t, std::error_code &ec) {
      using element_t = typename std::tuple_element_t<I, Tuple>;
      auto res = read<element_t>();
      if (res) {
         std::get<I>(t) = res.value();
         return true;
      } else {
         ec = res.error();
         return false;
      }
   }

   template <typename Tuple, std::size_t... I>
   void_t read_tuple_impl(Tuple &t, std::index_sequence<I...>) {
      std::error_code ec;
      const bool success = (read_tuple_element<Tuple, I>(t, ec) && ...);
      if (!success) {
         return tl::unexpected{ec};
      } else {
         return {};
      }
   }

   template <typename Tuple>
   expected<Tuple> read_tuple() {
      Tuple res;
      const auto op_res = read_tuple_impl(res, std::make_index_sequence<std::tuple_size_v<Tuple>>{});
      if (!op_res) {
         return tl::unexpected{op_res.error()};
      }
      return res;
   }

   template <std::unsigned_integral T>
   expected<T> read() {
      constexpr auto size = sizeof(T);
      auto ec = receive(size);
      if (!ec) {
         return tl::unexpected(ec.error());
      }

      if (size == 1) {
         // Only one byte - just return it after casting
         return static_cast<T>(recv_buffer_[0]);
      }

      // Decode as little-endian
      T result{};
      for (std::size_t i = 0; i < size; ++i) {
         if (i == 0) {
            result |= recv_buffer_[i];
            continue;
         }

         const T tmp = static_cast<T>(static_cast<T>(recv_buffer_[i]) << (i * 8));
         result |= tmp;
      }

      return result;
   }

   [[nodiscard]] void_t receive(std::size_t num_bytes) {
      fd_set read_fds{}, err_fds{};
      timeval tv{};

      std::size_t offset = 0;
      while (true) {
         if (offset == num_bytes) {
            // Done reading requested number of bytes
            return {};
         }

         FD_ZERO(&read_fds);
         FD_ZERO(&err_fds);
         FD_SET(socket_, &read_fds);
         FD_SET(socket_, &err_fds);

         tv.tv_sec = CONFIG_APP_IMAGE_CLIENT_READ_TIMEOUT_SEC;
         tv.tv_usec = 0;

         const int activity = select(socket_ + 1, &read_fds, nullptr, &err_fds, &tv);
         if (activity < 0) {
            return report_error("Read select error", errno);
         }

         if (activity == 0) {
            LOG_ERR("Receive timeout");
            return unexpected(ETIMEDOUT);
         }

         if (FD_ISSET(socket_, &err_fds)) {
            int error = 0;
            socklen_t len = sizeof(error);
            if (getsockopt(socket_, SOL_SOCKET, SO_ERROR, &error, &len) < 0) {
               LOG_ERR("Unknown read socket error");
               return unexpected(EIO);
            } else if (error != 0) {
               return report_error("Read socket error", error);
            }
            return unexpected(EIO);
         }

         if (!FD_ISSET(socket_, &read_fds)) {
            continue;
         }

         const ssize_t num_received = ::read(socket_, recv_buffer_.data() + offset, num_bytes - offset);
         if (num_received == 0) {
            LOG_ERR("Read connection closed");
            return unexpected(ECONNRESET);
         }

         if (num_received > 0) {
            offset += num_received;
            continue;
         }

         const int error = errno;
         if (error != EWOULDBLOCK && error != EAGAIN) {
            return report_error("Read error", error);
         }
      }
   }

   [[nodiscard]] void_t send(std::span<std::uint8_t, std::dynamic_extent> payload) const {
      fd_set write_fds{}, err_fds{};
      timeval tv{};

      std::size_t offset = 0;
      while (true) {
         if (offset == payload.size()) {
            // Done writing requested number of bytes
            return {};
         }

         FD_ZERO(&write_fds);
         FD_ZERO(&err_fds);
         FD_SET(socket_, &write_fds);
         FD_SET(socket_, &err_fds);

         tv.tv_sec = CONFIG_APP_IMAGE_CLIENT_READ_TIMEOUT_SEC;
         tv.tv_usec = 0;

         const int activity = select(socket_ + 1, nullptr, &write_fds, &err_fds, &tv);
         if (activity < 0) {
            return report_error("Send select error", errno);
         }

         if (activity == 0) {
            LOG_ERR("Send timeout");
            return unexpected(ETIMEDOUT);
         }

         if (FD_ISSET(socket_, &err_fds)) {
            int error = 0;
            socklen_t len = sizeof(error);
            if (getsockopt(socket_, SOL_SOCKET, SO_ERROR, &error, &len) < 0) {
               LOG_ERR("Unknown send socket error");
               return unexpected(EIO);
            } else if (error != 0) {
               return report_error("Send socket error", error);
            }
            return unexpected(EIO);
         }

         if (!FD_ISSET(socket_, &write_fds)) {
            continue;
         }

         const ssize_t num_sent = ::send(socket_, payload.data() + offset, payload.size() - offset, 0);
         if (num_sent == 0) {
            LOG_ERR("Send connection closed");
            return unexpected(ECONNRESET);
         }

         if (num_sent > 0) {
            offset += num_sent;
            continue;
         }

         const int error = errno;
         if (error != EWOULDBLOCK && error != EAGAIN) {
            return report_error("Send error", error);
         }
      }
   }

private:
   sockaddr_in server_address_{};
   int socket_{};
   std::array<std::uint8_t, CONFIG_APP_IMAGE_CLIENT_RECV_BUFFER_SIZE> recv_buffer_{};
   std::array<std::uint8_t, CONFIG_APP_IMAGE_CLIENT_IMAGE_BUFFER_SIZE> image_buffer_{};
};

image_client client{};

K_THREAD_STACK_DEFINE(image_client_thread_stack, CONFIG_APP_IMAGE_CLIENT_THREAD_STACK_SIZE);

K_THREAD_DEFINE(image_client_thread_id,
                CONFIG_APP_IMAGE_CLIENT_THREAD_STACK_SIZE,
                image_client::thread_fn,
                &client,
                nullptr,
                nullptr,
                CONFIG_APP_IMAGE_CLIENT_THREAD_PRIORITY,
                0,
                0);

} // namespace

namespace hei::image_client {

void start() {
   k_event_post(&client_events, ce_start);
}

} // namespace hei::image_client

#if CONFIG_SHELL

namespace {

int shell_do_fetch(const shell *sh, size_t argc, const char **argv) {
   ARG_UNUSED(sh);
   ARG_UNUSED(argc);
   ARG_UNUSED(argv);

   k_event_post(&client_events, ce_manual_fetch);
   return 0;
}

int shell_do_stop(const shell *sh, size_t argc, const char **argv) {
   ARG_UNUSED(sh);
   ARG_UNUSED(argc);
   ARG_UNUSED(argv);

   k_event_post(&client_events, ce_stop);
   return 0;
}

int dummy_help(const shell *sh, size_t argc, const char **argv) {
   if (argc == 1) {
      shell_help(sh);
      return 1;
   }

   shell_error(sh, "%s unknown command: %s", argv[0], argv[1]);
   return -EINVAL;
}

} // namespace

SHELL_STATIC_SUBCMD_SET_CREATE(image_client_commands,
                               SHELL_CMD_ARG(fetch, NULL, "Fetch an image", shell_do_fetch, 1, 0),
                               SHELL_CMD_ARG(stop, NULL, "Stop the image client", shell_do_stop, 1, 0),
                               SHELL_SUBCMD_SET_END);

SHELL_SUBCMD_ADD((hei), image_client, &image_client_commands, "Image Client shell", dummy_help, 2, 0);

#endif // CONFIG_SHELL
