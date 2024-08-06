/**
 * @file   wifi.cpp
 * @author Dennis Sitelew
 * @date   Aug. 02, 2024
 */

#include <hei/settings.hpp>
#include <hei/wifi.hpp>

#include <zephyr/net/net_event.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/net_config.h>
#include <zephyr/net/dhcpv4_server.h>

#include <zephyr/logging/log.h>

#include <array>
#include <exception>
#include <system_error>

#include <autoconf.h>

LOG_MODULE_REGISTER(hei_wifi, CONFIG_APP_LOG_LEVEL);

namespace {

namespace wifi {

enum event : std::uint32_t {
   //! Connected to a Wi-Fi network
   connected = BIT(0),

   //! Disconnected from a Wi-Fi network
   disconnected = BIT(1),

   //! Obtained an IPv4 address
   ipv4_assigned = BIT(2),

   //! Generic Wi-Fi error
   error = BIT(3),
};

//! Wi-Fi events
K_EVENT_DEFINE(state)

} // namespace wifi

struct net_mgmt_event_callback wifi_cb;
struct net_mgmt_event_callback ipv4_cb;

auto to_string(const in_addr &address) {
   std::array<char, NET_IPV4_ADDR_LEN> result{"invalid"};
   net_addr_ntop(AF_INET, &address, result.data(), result.size());
   return result;
}

void print_ipv4_addresses(const net_if &iface) {
   for (auto &address : iface.config.ip.ipv4->unicast) {
      const auto ip = to_string(address.ipv4.address.in_addr);
      const auto mask = to_string(address.netmask);
      LOG_DBG("IPv4 address: %s", ip.data());
      LOG_DBG("Mask: %s", mask.data());
   }

   const auto gateway = to_string(iface.config.ip.ipv4->gw);
   LOG_DBG("Gateway: %s", gateway.data());
}

void handle_wifi_connect_result(net_mgmt_event_callback &cb) {
   const auto status = reinterpret_cast<const struct wifi_status *>(cb.info);

   if (status->status) {
      LOG_ERR("Connection request failed: %d", status->status);
      k_event_set(&wifi::state, wifi::event::error);
   } else {
      LOG_DBG("Connected");
      k_event_set(&wifi::state, wifi::event::connected);

      // TODO: ESP32 driver doesn't distinguish between STA and AP connections
   }
}

void handle_wifi_disconnect_result(net_mgmt_event_callback &cb) {
   const auto status = reinterpret_cast<const struct wifi_status *>(cb.info);
   LOG_WRN("Disconnected, reason: %d", status->status);
   k_event_set(&wifi::state, wifi::event::error);

   // TODO: ESP32 driver doesn't distinguish between STA and AP disconnections
}

void wifi_mgmt_event_handler(struct net_mgmt_event_callback *cb, uint32_t mgmt_event, struct net_if *iface) {
   switch (mgmt_event) {
      case NET_EVENT_WIFI_CONNECT_RESULT:
         handle_wifi_connect_result(*cb);
         break;

      case NET_EVENT_WIFI_DISCONNECT_RESULT:
         handle_wifi_disconnect_result(*cb);
         break;

      case NET_EVENT_IPV4_ADDR_ADD:
         LOG_DBG("Got IP address");
         print_ipv4_addresses(*iface);
         k_event_set(&wifi::state, wifi::event::ipv4_assigned);
         break;

      case NET_EVENT_L4_CONNECTED:
         LOG_DBG("L4 connected");
         print_ipv4_addresses(*iface);
         break;

      default:
         break;
   }
}

[[noreturn]] void throw_system_error(std::errc code) {
   throw std::system_error(std::make_error_code(code));
}

void connect(net_if &iface) {
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
   if (auto err = net_mgmt(NET_REQUEST_WIFI_CONNECT, &iface, &wifi_params, sizeof(struct wifi_connect_req_params))) {
      LOG_ERR("Wi-Fi connection request failed: %d", err);
      throw_system_error(std::errc::invalid_argument);
   }

   auto events = k_event_wait(&wifi::state, wifi::event::ipv4_assigned | wifi::event::error, false,
                              K_SECONDS(CONFIG_APP_WIFI_CONNECTION_TIMEOUT));
   if (events & wifi::event::ipv4_assigned) {
      LOG_INF("Connected to \"%s\" network", ssid->data());
      return;
   }

   throw_system_error(std::errc::network_unreachable);
}

void configure_ap_ip(net_if &iface) {
   in_addr addr{}, netmask{};
   const char *address = "192.168.0.1", *mask = "255.255.255.0";
   if (net_addr_pton(AF_INET, address, &addr)) {
      LOG_ERR("Invalid address: %s", address);
      return;
   }

   /* If not empty */
   if (net_addr_pton(AF_INET, mask, &netmask)) {
      NET_ERR("Invalid netmask: %s", mask);
      return;
   }

   net_if_ipv4_addr_add(&iface, &addr, NET_ADDR_MANUAL, 0);
   net_if_ipv4_set_netmask_by_addr(&iface, &addr, &netmask);
}

void configure_dhcp_server(net_if &iface) {
   in_addr addr{};
   const char *address = "192.168.0.100";
   if (net_addr_pton(AF_INET, address, &addr)) {
      LOG_ERR("Invalid address: %s", address);
      return;
   }

   if (auto err = net_dhcpv4_server_start(&iface, &addr)) {
      LOG_ERR("DHCP server start error: %d", err);
   }
}

void host(net_if &iface) {
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

   LOG_DBG("Hosting AP \"%s\"", wifi_params.ssid);
   if (auto err = net_mgmt(NET_REQUEST_WIFI_AP_ENABLE, &iface, &wifi_params, sizeof(struct wifi_connect_req_params))) {
      LOG_ERR("Wi-Fi AP request failed: %d", err);
      throw_system_error(std::errc::invalid_argument);
   }

   // ESP32 doesn't generate AP-related events, so we can only hope that everything works
   LOG_INF("Hosting \"%s\" network", ssid.data());

   configure_ap_ip(iface);

   // TODO: Wait for L4 event
   // configure_dhcp_server(iface);

   print_ipv4_addresses(iface);
}

} // namespace

namespace hei::wifi {

void setup() {
   net_mgmt_init_event_callback(&wifi_cb, wifi_mgmt_event_handler,
                                NET_EVENT_WIFI_CONNECT_RESULT | NET_EVENT_WIFI_DISCONNECT_RESULT);
   net_mgmt_add_event_callback(&wifi_cb);

   net_mgmt_init_event_callback(&ipv4_cb, wifi_mgmt_event_handler, NET_EVENT_IPV4_ADDR_ADD | NET_EVENT_L4_CONNECTED);
   net_mgmt_add_event_callback(&ipv4_cb);

   net_if *iface = net_if_get_default();
   if (!iface) {
      LOG_ERR("No network interface available");
      throw_system_error(std::errc::no_such_device);
   }

#if 0
   if (hei::settings::configured()) {
      try {
         LOG_DBG("Application is fully configured, trying to connect");
         connect(*iface);
         return;
      } catch (const std::exception &e) {
         LOG_ERR("Wi-Fi connection error: %s", e.what());
         // Connect failed, continue with AP setup
      }
   }
#endif

   LOG_DBG("Trying to host");
   host(*iface);
}

} // namespace hei::wifi
