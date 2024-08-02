/**
 * @file   settings.cpp
 * @author Dennis Sitelew
 * @date   Aug. 02, 2024
 */

#include <hei/settings.hpp>

#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

#include <cstring>

#if CONFIG_SHELL
#include <zephyr/shell/shell.h>
#endif

LOG_MODULE_REGISTER(hei_settings, CONFIG_APP_LOG_LEVEL);

#define HEI_KEY "hei"
#define HEI_NAME(x) x, HEI_KEY "/" x

using namespace hei::settings;

namespace {

////////////////////////////////////////////////////////////////////////////////
/// Class: loadable
////////////////////////////////////////////////////////////////////////////////
class loadable {
public:
   explicit loadable(const char *key, const char *path)
      : key_{key}
      , path_{path} {
      // Nothing to do here
   }

   virtual ~loadable() = default;

public:
   bool should_load(const char *key) const {
      const char *next;
      return settings_name_steq(key, key_, &next) && !next;
   }

   bool is_loaded() const { return is_loaded_; }

   virtual int load(std::size_t len, settings_read_cb read_cb, void *cb_arg) = 0;
#if CONFIG_SHELL
   virtual int shell(const shell *sh, char **argv, std::size_t argc) = 0;
#endif

protected:
   const char *key_;
   const char *path_;
   bool is_loaded_{false};
};

////////////////////////////////////////////////////////////////////////////////
/// Class: loadable_int
////////////////////////////////////////////////////////////////////////////////
template <std::integral T>
class loadable_int : public loadable {
public:
   using loadable::loadable;

public:
   int load(std::size_t len, settings_read_cb read_cb, void *cb_arg) override {
      if (len != sizeof(T)) {
         return -EINVAL;
      }

      auto rc = read_cb(cb_arg, &value_, len);
      if (rc >= 0) {
         is_loaded_ = true;
         LOG_INF("Loaded %s: %" PRIi64, key_, static_cast<std::int64_t>(value_));
         return 0;
      }

      return rc;
   }

   std::optional<T> get() {
      if (!is_loaded_) {
         return std::nullopt;
      }

      return value_;
   }

   bool set(T value) {
      value_ = value;
      is_loaded_ = true;

      int res = settings_save_one(path_, &value_, sizeof(value_));
      if (res) {
         LOG_ERR("settings_save_one(%s) failed: %d", key_, res);
      }

      return res == 0;
   }

#if CONFIG_SHELL
   int shell(const struct shell *sh, char **argv, std::size_t argc) override {
      if (argc == 2) {
         // Set
         auto str_value = argv[1];
         const int base = 10;
         int err = 0;
         const long value = shell_strtol(str_value, base, &err);
         if (err) {
            shell_error(sh, "Invalid %s value: %s", key_, str_value);
            return -1;
         }

         if (set(value)) {
            shell_print(sh, "%s updated", key_);
            return 0;
         }

         shell_error(sh, "Error setting %s to \"%s\"", key_, str_value);
         return -1;
      } else if (argc == 1) {
         // Get
         if (is_loaded_) {
            shell_print(sh, "%s: %" PRIi64, key_, static_cast<std::int64_t>(value_));
         } else {
            shell_print(sh, "%s: [not set]", key_);
         }
         return 0;
      }
      return -1;
   }
#endif

private:
   T value_{};
};

////////////////////////////////////////////////////////////////////////////////
/// Class: loadable_string
////////////////////////////////////////////////////////////////////////////////
template <std::size_t MAX_STRING_SIZE = 128 + 1> // +1 for \0
class loadable_string : public loadable {
private:
   using string_t = std::array<char, MAX_STRING_SIZE>;

public:
   using loadable::loadable;

public:
   int load(std::size_t len, settings_read_cb read_cb, void *cb_arg) override {
      if (len >= storage_.size()) {
         return -EINVAL;
      }

      auto rc = read_cb(cb_arg, storage_.data(), len);
      if (rc >= 0) {
         storage_[len] = '\0';
         value_ = {storage_.data(), len};
         is_loaded_ = true;

         LOG_INF("Loaded %s: \"%s\"", key_, storage_.data());
         return 0;
      }

      return rc;
   }

   optional_span_t get() {
      if (!is_loaded_) {
         return std::nullopt;
      }

      return value_;
   }

   bool set(span_t value) {
      if (value.size() >= MAX_STRING_SIZE) {
         LOG_ERR("Invalid %s value size: %zu", key_, value.size());
         return false;
      }

      using namespace std;
      copy(begin(value), end(value), begin(storage_));

      storage_[value.size()] = '\0';
      value_ = {storage_.data(), value.size()};
      is_loaded_ = true;

      int res = settings_save_one(path_, storage_.data(), value_.size());
      if (res) {
         LOG_ERR("settings_save_one(%s) failed: %d", key_, res);
      }

      return res == 0;
   }

#if CONFIG_SHELL
   int shell(const struct shell *sh, char **argv, std::size_t argc) override {
      if (argc == 2) {
         // Set
         auto str_value = argv[1];
         auto length = std::strlen(str_value);
         auto res = set({str_value, length});
         if (res) {
            shell_print(sh, "%s updated", key_);
            return 0;
         }

         shell_error(sh, "Error setting %s to \"%s\"", key_, str_value);
         return -1;
      } else if (argc == 1) {
         // Get
         if (is_loaded_) {
            shell_print(sh, "%s: \"%s\"", key_, value_.data());
         } else {
            shell_print(sh, "%s: [not set]", key_);
         }
         return 0;
      }
      return -1;
   }
#endif

private:
   string_t storage_{'\0'};           //! Storage for the loaded data
   span_t value_{storage_.data(), 0}; //! Wrapper around the storage with the correct size
};

using loadable_string_t = loadable_string<>;

////////////////////////////////////////////////////////////////////////////////
/// Configuration storage
////////////////////////////////////////////////////////////////////////////////
struct wifi_config {
   loadable_string_t ssid{HEI_NAME("wifi-ssid")};
   loadable_string_t password{HEI_NAME("wifi-password")};
};

struct image_server_config {
   loadable_string_t address{HEI_NAME("image-server-address")};
   loadable_int<std::uint16_t> port{HEI_NAME("image-server-port")};
   loadable_int<std::uint16_t> refresh_interval{HEI_NAME("image-server-refresh-interval")};
};

struct app_config {
   wifi_config wifi{};
   image_server_config image_server{};
};

app_config config{};

auto all_options() {
   auto base = [](auto &v) {
      return static_cast<loadable *>(&v);
   };

   return std::array{
      base(config.wifi.ssid),
      base(config.wifi.password),
      base(config.image_server.address),
      base(config.image_server.port),
      base(config.image_server.refresh_interval),
   };
}

////////////////////////////////////////////////////////////////////////////////
/// Configuration Loading
////////////////////////////////////////////////////////////////////////////////
int settings_set_handler(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg) {
   for (auto o : all_options()) {
      if (o->should_load(name)) {
         return o->load(len, read_cb, cb_arg);
      }
   }

   return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(hei_handler, HEI_KEY, nullptr, settings_set_handler, nullptr, nullptr);

int settings_init() {
   if (auto err = settings_subsys_init()) {
      LOG_ERR("Settings initialization failed: %d", err);
      return -1;
   }

   if (auto err = settings_load_subtree(HEI_KEY)) {
      LOG_ERR("Settings load failed: %d", err);
      return -1;
   }

   return 0;
}

SYS_INIT(settings_init, APPLICATION, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);

} // namespace

namespace hei::settings {

namespace wifi {

optional_span_t ssid() {
   return config.wifi.ssid.get();
}

optional_span_t password() {
   return config.wifi.password.get();
}

} // namespace wifi

namespace image_server {

optional_span_t address() {
   return config.image_server.address.get();
}

std::optional<std::uint16_t> port() {
   return config.image_server.port.get();
}

std::optional<std::chrono::seconds> refresh_interval() {
   auto opt = config.image_server.refresh_interval.get();
   if (!opt) {
      return std::nullopt;
   }

   return std::chrono::seconds{*opt};
}

} // namespace image_server

bool configured() {
   for (auto o : all_options()) {
      if (!o->is_loaded()) {
         return false;
      }
   }
   return true;
}

} // namespace hei::settings

#if CONFIG_SHELL

namespace {

int shell_wifi_ssid(const shell *sh, size_t argc, char **argv) {
   return config.wifi.ssid.shell(sh, argv, argc);
}

int shell_wifi_password(const shell *sh, size_t argc, char **argv) {
   return config.wifi.password.shell(sh, argv, argc);
}

int shell_image_server_address(const shell *sh, size_t argc, char **argv) {
   return config.image_server.address.shell(sh, argv, argc);
}

int shell_image_server_port(const shell *sh, size_t argc, char **argv) {
   return config.image_server.port.shell(sh, argv, argc);
}

int shell_is_refresh_interval(const shell *sh, size_t argc, char **argv) {
   return config.image_server.refresh_interval.shell(sh, argv, argc);
}

int dummy_help(const shell *sh, size_t argc, char **argv) {
   if (argc == 1) {
      shell_help(sh);
      return 1;
   }

   shell_error(sh, "%s unknown command: %s", argv[0], argv[1]);
   return -EINVAL;
}

int print_current(const shell *sh, size_t argc, char **argv) {
   ARG_UNUSED(argc);

   for (auto o : all_options()) {
      o->shell(sh, argv, 1);
   }

   return 0;
}

} // namespace

SHELL_STATIC_SUBCMD_SET_CREATE(wifi_commands,
                               SHELL_CMD_ARG(ssid, NULL, "Get or set Wi-Fi SSID", shell_wifi_ssid, 1, 1),
                               SHELL_CMD_ARG(password, NULL, "Get or set Wi-Fi password", shell_wifi_password, 1, 1),
                               SHELL_SUBCMD_SET_END);

SHELL_STATIC_SUBCMD_SET_CREATE(
   image_server_commands,
   SHELL_CMD_ARG(addresss, NULL, "Get or set image server address", shell_image_server_address, 1, 1),
   SHELL_CMD_ARG(port, NULL, "Get or set image server port", shell_image_server_port, 1, 1),
   SHELL_CMD_ARG(refresh_interval, NULL, "Get or set image server refresh interval", shell_is_refresh_interval, 1, 1),
   SHELL_SUBCMD_SET_END);

SHELL_STATIC_SUBCMD_SET_CREATE(settings_commands,
                               SHELL_CMD_ARG(wifi, &wifi_commands, "Wi-Fi commands", dummy_help, 2, 0),
                               SHELL_CMD_ARG(image_server, &image_server_commands, "Image Server", dummy_help, 2, 0),
                               SHELL_CMD_ARG(print, NULL, "Print the current settings", print_current, 1, 0),
                               SHELL_SUBCMD_SET_END);

SHELL_SUBCMD_ADD((hei), settings, &settings_commands, "Set Wi-Fi SSID", dummy_help, 2, 0);

#endif // CONFIG_SHELL
