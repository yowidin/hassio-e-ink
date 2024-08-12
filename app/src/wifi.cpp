/**
 * @file   wifi.cpp
 * @author Dennis Sitelew
 * @date   Aug. 02, 2024
 */

#include <hei/settings.hpp>
#include <hei/wifi.hpp>

#include <zephyr/net/dhcpv4_server.h>
#include <zephyr/net/net_config.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi_mgmt.h>

#include <zephyr/logging/log.h>

#include <array>
#include <exception>
#include <functional>
#include <system_error>

#include <autoconf.h>

LOG_MODULE_REGISTER(hei_wifi, CONFIG_APP_LOG_LEVEL);

namespace {

class wifi_manager {
private:
   // Wi-Fi events: declared as simple enum to simplify bitwise operations
   enum event : std::uint32_t {
      //! Connected to a Wi-Fi network
      connected = BIT(0),

      //! Disconnected from a Wi-Fi network
      disconnected = BIT(1),

      //! L4 connection established (interface is ready and got an IP)
      l4_connected = BIT(2),

      //! Generic Wi-Fi error
      error = BIT(12),
   };

   struct net_event_cb_holder {
      net_event_cb_holder(wifi_manager &owner, std::uint32_t mask)
         : owner_{&owner}
         , cb_{} {
         static_assert(std::is_standard_layout_v<net_event_cb_holder>);

         net_mgmt_init_event_callback(
            &cb_,
            [](net_mgmt_event_callback *cb, uint32_t event, net_if *iface) {
               ARG_UNUSED(iface);

               auto holder = CONTAINER_OF(cb, net_event_cb_holder, cb_);
               holder->owner_->net_event_handler(event, cb->info);
            },
            mask);
         net_mgmt_add_event_callback(&cb_);
      }

      wifi_manager *owner_;
      net_mgmt_event_callback cb_;
   };

public:
   wifi_manager()
      : state_{}
      , wifi_{*this, NET_EVENT_WIFI_CONNECT_RESULT | NET_EVENT_WIFI_DISCONNECT_RESULT}
      , l4_{*this, NET_EVENT_L4_CONNECTED} {
      k_event_init(&state_);

      iface_ = net_if_get_default();
      if (!iface_) {
         LOG_ERR("No network interface available");
         throw_system_error(std::errc::no_such_device);
      }
   }

public:
   void start() {
      if (!connect()) {
         host();
      }
   }

private:
   static auto to_string(const in_addr &address) {
      std::array<char, NET_IPV4_ADDR_LEN> result{"invalid"};
      net_addr_ntop(AF_INET, &address, result.data(), result.size());
      return result;
   }

   static auto from_string(const char *address) {
      in_addr result{};
      if (net_addr_pton(AF_INET, address, &result)) {
         LOG_ERR("Invalid address: %s", address);
         throw_system_error(std::errc::bad_address);
      }
      return result;
   }

   [[noreturn]] static void throw_system_error(std::errc code) { throw std::system_error(std::make_error_code(code)); }

private:
   void clear_events() { k_event_clear(&state_, std::numeric_limits<std::uint32_t>::max()); }

   void print_ipv4_addresses() {
      for (auto &address : iface_->config.ip.ipv4->unicast) {
         const auto ip = to_string(address.ipv4.address.in_addr);
         const auto mask = to_string(address.netmask);
         LOG_DBG("IPv4 address: %s", ip.data());
         LOG_DBG("Mask: %s", mask.data());
      }

      const auto gateway = to_string(iface_->config.ip.ipv4->gw);
      LOG_DBG("Gateway: %s", gateway.data());
   }

   void handle_connect_result(const void *info) {
      const auto status = reinterpret_cast<const struct wifi_status *>(info);
      if (status->status) {
         LOG_ERR("Connection request failed: %d", status->status);
         k_event_set(&state_, event::error);
      } else {
         LOG_DBG("Wi-Fi connected");
         k_event_set(&state_, event::connected);
      }
   }

   void handle_disconnect_result(const void *info) {
      const auto status = reinterpret_cast<const struct wifi_status *>(info);
      LOG_WRN("Disconnected, reason: %d", status->status);
      k_event_set(&state_, event::error);
   }

   void net_event_handler(std::uint32_t event, const void *info) {
      switch (event) {
         case NET_EVENT_WIFI_CONNECT_RESULT:
            handle_connect_result(info);
            break;

         case NET_EVENT_WIFI_DISCONNECT_RESULT:
            handle_disconnect_result(info);
            break;

         case NET_EVENT_L4_CONNECTED:
            LOG_DBG("L4 connected");
            k_event_set(&state_, event::l4_connected);
            break;

         default:
            break;
      }
   }

   void try_connect() {
      auto ssid = hei::settings::wifi::ssid();
      auto password = hei::settings::wifi::password();
      auto security = hei::settings::wifi::security();
      if (!ssid || !password || !security) {
         throw_system_error(std::errc::not_supported);
      }

      wifi_connect_req_params wifi_params = {
         .ssid = reinterpret_cast<const std::uint8_t *>(ssid->data()),
         .ssid_length = static_cast<std::uint8_t>(ssid->size()),

         .psk = reinterpret_cast<const std::uint8_t *>(password->data()),
         .psk_length = static_cast<std::uint8_t>(password->size()),

         .band = WIFI_FREQ_BAND_2_4_GHZ,
         .channel = WIFI_CHANNEL_ANY,
         .security = static_cast<wifi_security_type>(*security),
         .mfp = WIFI_MFP_OPTIONAL,

         .timeout = CONFIG_APP_WIFI_CONNECTION_TIMEOUT,
      };

      LOG_DBG("Connecting to \"%s\"", wifi_params.ssid);
      if (auto err = net_mgmt(NET_REQUEST_WIFI_CONNECT, iface_, &wifi_params, sizeof(struct wifi_connect_req_params))) {
         LOG_ERR("Wi-Fi connection request failed: %d", err);
         throw_system_error(std::errc{err});
      }

      auto events =
         k_event_wait(&state_, event::connected | event::error, false, K_SECONDS(CONFIG_APP_WIFI_CONNECTION_TIMEOUT));
      if ((events & event::connected) != event::connected) {
         LOG_ERR("Connection error");
         throw_system_error(std::errc::network_unreachable);
      }

      LOG_DBG("Connection started, waiting for IP");
      net_dhcpv4_start(iface_);

      events = k_event_wait(&state_, event::l4_connected | event::error, false,
                            K_SECONDS(CONFIG_APP_WIFI_CONNECTION_TIMEOUT));
      if ((events & event::l4_connected) != event::l4_connected) {
         LOG_ERR("Error getting IPv4");
         throw_system_error(std::errc::network_unreachable);
      }

      LOG_INF("Connected to \"%s\" network", ssid->data());
      print_ipv4_addresses();
   }

   bool connect() {
      if (!hei::settings::configured()) {
         return false;
      }

      LOG_DBG("Application is fully configured, trying to connect");
      for (int i = 0; i < CONFIG_APP_WIFI_CONNECTION_ATTEMPTS; ++i) {
         clear_events();

         try {
            try_connect();
            return true;
         } catch (const std::exception &e) {
            LOG_ERR("Wi-Fi connection attempt #%d error: %s", i, e.what());
            k_sleep(K_SECONDS(5));
         }
      }

      return false;
   }

   void host() {
      LOG_DBG("Trying to host");

      clear_events();

      auto ssid = hei::settings::wifi::ap::ssid();
      auto password = hei::settings::wifi::ap::password();

      wifi_connect_req_params wifi_params = {
         .ssid = reinterpret_cast<const std::uint8_t *>(ssid.data()),
         .ssid_length = static_cast<std::uint8_t>(ssid.size()),

         .psk = reinterpret_cast<const std::uint8_t *>(password.data()),
         .psk_length = static_cast<std::uint8_t>(password.size()),

         .band = WIFI_FREQ_BAND_2_4_GHZ,
         .channel = WIFI_CHANNEL_ANY,
         .security = WIFI_SECURITY_TYPE_PSK,
         .mfp = WIFI_MFP_OPTIONAL,

         .timeout = CONFIG_APP_WIFI_CONNECTION_TIMEOUT,
      };

      LOG_DBG("Enabling hosting AP \"%s\"", wifi_params.ssid);
      if (auto err =
             net_mgmt(NET_REQUEST_WIFI_AP_ENABLE, iface_, &wifi_params, sizeof(struct wifi_connect_req_params))) {
         LOG_ERR("Wi-Fi AP request failed: %d", err);
         throw_system_error(std::errc{err});
      }

      LOG_DBG("Waiting for L4");
      configure_ap_ip();

      auto events = k_event_wait(&state_, event::l4_connected | event::error, false,
                                 K_SECONDS(CONFIG_APP_WIFI_CONNECTION_TIMEOUT));
      if ((events & event::l4_connected) != event::l4_connected) {
         LOG_ERR("AP configuration failed: %d", events);
         throw_system_error(std::errc::network_unreachable);
      }

      LOG_INF("Configuring host network");
      configure_dhcp_server();
      print_ipv4_addresses();
   }

   void configure_ap_ip() {
      auto addr = from_string("192.168.0.1");
      auto mask = from_string("255.255.255.0");

      if (!net_if_ipv4_addr_add(iface_, &addr, NET_ADDR_MANUAL, 0)) {
         LOG_ERR("Set manual IP failed");
         throw_system_error(std::errc::network_down);
      }

      if (!net_if_ipv4_set_netmask_by_addr(iface_, &addr, &mask)) {
         LOG_ERR("Set netmask failed");
         throw_system_error(std::errc::network_down);
      }
   }

   void configure_dhcp_server() {
      auto addr = from_string("192.168.0.100");
      if (auto err = net_dhcpv4_server_start(iface_, &addr)) {
         LOG_ERR("DHCP server start error: %d", err);
         throw_system_error(std::errc{err});
      }
   }

private:
   k_event state_;
   net_if *iface_;

   net_event_cb_holder wifi_;
   net_event_cb_holder l4_;
};

} // namespace

namespace hei::wifi {

void setup() {
   static wifi_manager manager{};
   manager.start();
}

} // namespace hei::wifi
