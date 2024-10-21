/**
 * @file   image_client.cpp
 * @author Dennis Sitelew
 * @date   Sep. 30, 2024
 */

#include <hei/display.hpp>
#include <hei/image_client.hpp>
#include <hei/settings.hpp>
#include <hei/shutdown.hpp>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <cerrno>

#include <lz4.h>

#include <array>
#include <stdexcept>

#include <autoconf.h>

#if CONFIG_SHELL
#include <zephyr/shell/shell.h>
#endif

LOG_MODULE_REGISTER(image_client, CONFIG_APP_LOG_LEVEL);

namespace {

enum client_event_t {
   ce_start = BIT(0),
   ce_manual_fetch = BIT(1),
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
         try {
            const auto start = k_uptime_get();

            fetch_image();

            const auto end = k_uptime_get();
            const auto delta = end - start;
            LOG_INF("Received image in %" PRIi64 " ms", delta);
         } catch (const std::exception &e) {
            LOG_ERR("Image client error: %s", e.what());
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

         (void)k_event_wait(&client_events, ce_manual_fetch, true, K_SECONDS(sleep_duration.count()));
      }
   }

   static void throw_error(const char *message, int error) {
      LOG_ERR("%s: %s", message, strerror(error));
      throw std::system_error(std::make_error_code(std::errc{error}));
   }

   void fetch_image() {
      if ((socket_ = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
         throw_error("Socket creation error", errno);
      }

      if (connect(socket_, reinterpret_cast<sockaddr *>(&server_address_), sizeof(server_address_)) < 0) {
         throw_error("Connection failed", errno);
      }

      LOG_INF("Connected to server");

      // Set socket to non-blocking mode
      int flags = fcntl(socket_, F_GETFL, 0);
      if (flags < 0) {
         throw_error("Error getting socket flags", errno);
      }

      if (fcntl(socket_, F_SETFL, flags | O_NONBLOCK) < 0) {
         throw_error("Error setting socket flags", errno);
      }

      auto message = static_cast<std::uint8_t>(message_type::get_image_request);
      if (send(socket_, &message, sizeof(message), 0) < 0) {
         throw_error("Error sending request", errno);
      }

      // Read header: message_type: u8 | width: u16 | height: u16 | num_blocks: u16
      auto type = static_cast<message_type>(read<std::uint8_t>());
      if (type == message_type::server_error) {
         LOG_WRN("Server error");
         return;
      }

      if (type != message_type::image_header_response) {
         LOG_ERR("Bad response: %" PRIu8, static_cast<std::uint8_t>(type));
         return;
      }

      // x2 because the transmitted image is 4 bytes per pixel
      const auto image_width = read<std::uint16_t>() * 2;
      const auto image_height = read<std::uint16_t>();
      const auto num_blocks = read<std::uint16_t>();

      // TODO: get refresh type from the server
      hei::display::begin(image_width, image_height);

      LOG_DBG("Image Header: w=%" PRIu16 ", h=%" PRIu16 ", n=%" PRIu16, image_width, image_height, num_blocks);
      for (std::uint16_t block = 0; block < num_blocks; ++block) {
         type = static_cast<message_type>(read<std::uint8_t>());
         if (type == message_type::server_error) {
            LOG_WRN("Server error");
            return;
         }

         if (type != message_type::image_block_response) {
            LOG_ERR("Bad block type: %" PRIu8, static_cast<std::uint8_t>(type));
            return;
         }

         const auto uncompressed_size = read<std::uint16_t>();
         const auto compressed_size = read<std::uint16_t>();
         auto ec = receive(compressed_size);
         if (ec) {
            throw_error("Error receiving block", ec.value());
         }

         const auto res = LZ4_decompress_safe(reinterpret_cast<const char *>(recv_buffer_.data()),
                                              reinterpret_cast<char *>(image_buffer_.data()), compressed_size,
                                              static_cast<int>(image_buffer_.size()));
         if (res < 0) {
            LOG_ERR("Image block decompression error: %d", res);
            return;
         }

         if (res != uncompressed_size) {
            LOG_ERR("Decompressed data size mismatch: %d vs %d", res, static_cast<int>(uncompressed_size));
            return;
         }

         hei::display::update({image_buffer_.data(), uncompressed_size});
      }

      hei::display::end(image_width, image_height, hei::display::refresh::full);
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

   template <std::unsigned_integral T>
   T read() {
      constexpr auto size = sizeof(T);
      auto ec = receive(size);
      if (ec) {
         throw std::system_error{ec};
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

         const T tmp = static_cast<T>(static_cast<T>(recv_buffer_[i]) << i * 8);
         result |= tmp;
      }

      return result;
   }

   std::error_code receive(std::size_t num_bytes) {
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
            LOG_ERR("Select error: %s", strerror(errno));
            return std::make_error_code(std::errc{errno});
         }

         if (activity == 0) {
            LOG_ERR("Receive timeout");
            return std::make_error_code(std::errc::timed_out);
         }

         if (FD_ISSET(socket_, &err_fds)) {
            int error = 0;
            socklen_t len = sizeof(error);
            if (getsockopt(socket_, SOL_SOCKET, SO_ERROR, &error, &len) < 0) {
               LOG_ERR("Unknown socket error");
               return std::make_error_code(std::errc::io_error);
            } else if (error != 0) {
               LOG_ERR("Socket error: %s", strerror(error));
               return std::make_error_code(std::errc{error});
            }
            return std::make_error_code(std::errc::io_error);
         }

         if (!FD_ISSET(socket_, &read_fds)) {
            continue;
         }

         const ssize_t num_received = ::read(socket_, recv_buffer_.data() + offset, num_bytes - offset);
         if (num_received == 0) {
            LOG_ERR("Connection closed");
            return std::make_error_code(std::errc::not_connected);
         }

         if (num_received > 0) {
            offset += num_received;
            continue;
         }

         const int error = errno;
         if (error != EWOULDBLOCK && error != EAGAIN) {
            LOG_ERR("Read error: %s", strerror(error));
            return std::make_error_code(std::errc{error});
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

int shell_do_clear(const shell *sh, size_t argc, const char **argv) {
   ARG_UNUSED(sh);
   ARG_UNUSED(argc);
   ARG_UNUSED(argv);

   try {
      hei::display::clear();
   } catch (const std::exception &e) {
      shell_error(sh, "Error cleaning screen: %s", e.what());
      return -1;
   }

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
                               SHELL_CMD_ARG(clear, NULL, "Clear the screen", shell_do_clear, 1, 0),
                               SHELL_SUBCMD_SET_END);

SHELL_SUBCMD_ADD((hei), image_client, &image_client_commands, "Image Client shell", dummy_help, 2, 0);

#endif // CONFIG_SHELL
