/**
 * @file   display.cpp
 * @author Dennis Sitelew
 * @date   Oct. 03, 2024
 */

#include <hei/display.hpp>

#include <it8951/display.hpp>

#include <zephyr/logging/log.h>

#include <optional>
#include <stdexcept>

LOG_MODULE_REGISTER(display, CONFIG_APP_LOG_LEVEL);

#if CONFIG_SHELL
#include <zephyr/shell/shell.h>
#endif

namespace {

const struct device *const display_driver = DEVICE_DT_GET_ONE(ite_it8951);
it8951::display display{*display_driver};

} // namespace

namespace hei::display {

bool init() {
   if (!device_is_ready(display_driver)) {
      LOG_ERR("Display not ready: %s", display_driver->name);
      return false;
   }

   return true;
}

it8951::display &get() {
   return ::display;
}

} // namespace hei::display

#if CONFIG_SHELL

namespace {

template <typename T>
bool shell_parse(const shell *sh, const char *arg, const char *name, int base, T &out) {
   auto handle_err = [&](int err, auto value) {
      if (err == -EINVAL) {
         shell_error(sh, "Bad %s string: %s", name, arg);
         return false;
      } else if (err == -ERANGE) {
         shell_error(sh, "%s value out of range: %s", name, arg);
         return false;
      }

      out = static_cast<T>(value);
      return true;
   };

   int err;
   if constexpr (std::is_signed_v<T>) {
      static_assert(sizeof(T) <= sizeof(long));
      auto res = shell_strtol(arg, base, &err);
      return handle_err(err, res);
   } else {
      static_assert(sizeof(T) <= sizeof(unsigned long long));
      auto res = shell_strtoull(arg, base, &err);
      return handle_err(err, res);
   }
}

template <typename T>
std::optional<T> shell_parse(const shell *sh, const char *arg, const char *name, int base) {
   T res;
   if (!shell_parse(sh, arg, name, base, res)) {
      return {};
   }

   return res;
}

int shell_do_fill(const shell *sh, size_t argc, const char **argv) {
   ARG_UNUSED(argc);

   auto opt_pattern = shell_parse<std::uint64_t>(sh, argv[1], "pattern", 16);
   auto opt_mode = shell_parse<std::uint8_t>(sh, argv[2], "mode", 10);

   if (!opt_pattern || !opt_mode) {
      return -EINVAL;
   }

   auto mode = static_cast<it8951::common::waveform_mode>(*opt_mode);
   switch (mode) {
      case it8951::common::waveform_mode::init:
      case it8951::common::waveform_mode::direct_update:
      case it8951::common::waveform_mode::grayscale_clearing:
      case it8951::common::waveform_mode::grayscale_limited:
      case it8951::common::waveform_mode::grayscale_limited_reduced:
         break;
      default:
         shell_error(sh, "Invalid mode: %d", (int)mode);
         return -EINVAL;
   }

   const auto pattern = *opt_pattern;

   // We expect 4bpp encoding, so we have to split the uint64_t into 16 bytes
   std::array<std::uint8_t, 16> as_bytes{};
   for (int i = 7; i >= 0; --i) {
      std::uint8_t byte;
      if (i != 0) {
         byte = static_cast<std::uint8_t>((pattern >> (8 * i)) & 0xFF);
      } else {
         byte = static_cast<std::uint8_t>(pattern & 0xFF);
      }

      const uint8_t high = (byte >> 4) & 0xF;
      const uint8_t low = byte & 0xF;

      const auto idx = (7 - i) * 2;
      as_bytes[idx] = high;
      as_bytes[idx + 1] = low;
   }
   LOG_HEXDUMP_INF(as_bytes.data(), as_bytes.size(), "Pattern");

   auto width = display.width();

   std::error_code ec;
   ::display.fill_screen(
      [&](auto x, auto y) -> std::uint8_t {
         const auto idx = ((width * y) + x);
         return as_bytes[idx % as_bytes.size()];
      },
      mode, ec);

   if (ec) {
      shell_error(sh, "Error filling the screen: %s", ec.message().c_str());
      return -EINVAL;
   }

   return 0;
}

int shell_do_clear(const shell *sh, size_t argc, const char **argv) {
   ARG_UNUSED(sh);
   ARG_UNUSED(argc);
   ARG_UNUSED(argv);

   std::error_code ec;
   ::display.clear(ec);

   if (ec) {
      shell_error(sh, "Error cleaning screen: %s", ec.message().data());
      return -1;
   }

   return 0;
}

int dummy_help(const shell *sh, size_t argc, const char **argv) {
   if (argc == 1) {
      shell_help(sh);
      return 1;
   }

   static_assert(sizeof(unsigned long long) >= sizeof(std::uint64_t));

   shell_error(sh, "%s unknown command: %s", argv[0], argv[1]);
   return -EINVAL;
}

} // namespace

SHELL_STATIC_SUBCMD_SET_CREATE(display_commands,
                               SHELL_CMD_ARG(fill,
                                             NULL,
                                             R"help(Fill the screen with a multi-byte pattern.
Usage: fill <pattern> <mode>
- <pattern> B0B1B2B3B4B5B6B7B8 in hex, where B0 will be passed for pixels [0, 0] and [1, 0] etc., e.g. 123456789ABCDEF
- <mode> Waveform mode: 0 - init, 1 - DU, 2 - GC16, 3 - GL16, 4 - GLR16)help",
                                             shell_do_fill,
                                             3,
                                             0),
                               SHELL_CMD_ARG(clear, NULL, "Clear the screen", shell_do_clear, 1, 0),
                               SHELL_SUBCMD_SET_END);

SHELL_SUBCMD_ADD((hei), display, &display_commands, "Display shell", dummy_help, 2, 0);

#endif // CONFIG_SHELL
