/**
 * @file   wifi.cpp
 * @author Dennis Sitelew
 * @date   Aug. 02, 2024
 */

#include <hei/dns.hpp>
#include <hei/settings.hpp>
#include <hei/wifi.hpp>

#include <zephyr-cpp/mutex.hpp>

#include <zephyr/net/dhcpv4_server.h>
#include <zephyr/net/net_config.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi_mgmt.h>

#include <zephyr/logging/log.h>

#include <array>
#include <functional>
#include <mutex>

#include <autoconf.h>
#include <esp_mac.h>

LOG_MODULE_REGISTER(hei_wifi, CONFIG_APP_LOG_LEVEL);

using namespace zephyr;

namespace {

void_t mac_to_str(const std::uint8_t *mac, std::uint8_t mac_length, hei::wifi::mac_addr_t &str) {
   if (!mac_length) {
      snprintf(str.data(), str.size(), "[unknown]");
      return {};
   }

   const auto ret =
      snprintf(str.data(), str.size(), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

   if (ret < 0 || ret >= static_cast<int>(str.size())) {
      LOG_ERR("Error converting a MAC address: %d", ret);
      return unexpected(ENOMEM);
   }

   return {};
}

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
   explicit wifi_manager(net_if &iface)
      : state_{}
      , iface_{&iface}
      , wifi_{*this,
              NET_EVENT_WIFI_CONNECT_RESULT | NET_EVENT_WIFI_DISCONNECT_RESULT | NET_EVENT_WIFI_SCAN_RESULT
                 | NET_EVENT_WIFI_SCAN_DONE}
      , l4_{*this, NET_EVENT_L4_CONNECTED} {
      k_event_init(&state_);

      instance_ = this;

      k_work_init(&periodic_network_scan_work_, [](k_work *work) {
         ARG_UNUSED(work);
         instance_->start_network_scan();
      });

      k_timer_init(
         &periodic_network_scan_timer_,
         [](k_timer *timer) {
            ARG_UNUSED(timer);
            k_work_submit(&instance_->periodic_network_scan_work_);
         },
         nullptr);
   }

public:
   void_t start() {
      if (connect()) {
         // No point in doing network scans when not hosting, just return
         return update_mac_address();
      }

      return host().and_then([&]() {
         is_hosting_ = true;

         return update_mac_address().and_then([&]() -> void_t {
            // TODO: Somehow AP breaks if we do network scans regularly. Do a single scan just before hosting.
            static_assert((CONFIG_APP_NETWORK_SCAN_DURATION * 2) < CONFIG_APP_NETWORK_SCAN_INTERVAL);
            k_timer_start(&periodic_network_scan_timer_, K_NO_WAIT, K_SECONDS(CONFIG_APP_NETWORK_SCAN_INTERVAL));

            return {};
         });
      });
   }

   [[nodiscard]] bool is_hosting() const { return is_hosting_; }
   [[nodiscard]] std::string_view get_mac() const { return {mac_addr_.data(), mac_addr_.size() - 1}; }

   void with_networks(const hei::wifi::network_list_handler_t &cb) {
      std::unique_lock<decltype(scan_mutex_)> lock{scan_mutex_};
      cb(networks_, num_networks_);
   }

   static wifi_manager *get() { return instance_; }

private:
   static auto to_string(const in_addr &address) {
      std::array<char, NET_IPV4_ADDR_LEN> result{"invalid"};
      net_addr_ntop(AF_INET, &address, result.data(), result.size());
      return result;
   }

   static expected<in_addr> from_string(const char *address) {
      in_addr result{};
      if (net_addr_pton(AF_INET, address, &result)) {
         LOG_ERR("Invalid address: %s", address);
         return unexpected(EINVAL);
      }
      return result;
   }

private:
   void clear_events() { k_event_clear(&state_, std::numeric_limits<std::uint32_t>::max()); }

   void_t update_mac_address() {
      const auto mac_addr = net_if_get_link_addr(iface_);
      if (!mac_addr || !mac_addr->addr) {
         return unexpected(ENETDOWN);
      }

      if (mac_addr->len != 6) {
         LOG_ERR("Unexpected MAC address length: %" PRIu8, mac_addr->len);
         return unexpected(EINVAL);
      }

      return mac_to_str(mac_addr->addr, mac_addr->len, mac_addr_);
   }

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

   void handle_wifi_scan_result(const void *info) {
      const auto *entry = static_cast<const struct wifi_scan_result *>(info);
      hei::wifi::network candidate{*entry};

      using namespace std;
      unique_lock<decltype(scan_mutex_)> lock{scan_mutex_};

      // Replace existing network with same BSSID if found
      bool placed = false;
      for (size_t i = 0; i < num_networks_; ++i) {
         if (networks_[i] == candidate) {
            swap(networks_[i], candidate);
            placed = true;
            break;
         }
      }

      if (!placed) {
         if (num_networks_ < networks_.size()) {
            // Append to the end if still have space
            swap(networks_[num_networks_], candidate);
            num_networks_ += 1;
            placed = true;
         } else if (networks_[num_networks_ - 1] < candidate) {
            // Replace the last network if better RSSI
            swap(networks_[num_networks_ - 1], candidate);
            placed = true;
         }
      }

      if (placed) {
         sort(begin(networks_), begin(networks_) + num_networks_);
      }
   }

   void handle_wifi_scan_done(const void *info) {
      const auto status = static_cast<const struct wifi_status *>(info);
      if (status->status) {
         LOG_WRN("Wi-Fi scan failed: %d", status->status);
      } else {
         LOG_DBG("Wi-Fi scan done");
      }

      LOG_DBG("%-4s | %-32s | %-4s | %-4s | %-17s", "Num", "SSID", "Chan", "RSSI", "BSSID");
      std::unique_lock<decltype(scan_mutex_)> lock{scan_mutex_};
      for (std::size_t i = 0; i < num_networks_; ++i) {
         const auto &net = networks_[i];
         LOG_DBG("%-4d | %-32s | %-4u | %-4d | %-17s", (int)i, net.ssid.data(), net.channel, net.rssi, net.mac.data());
      }
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

         case NET_EVENT_WIFI_SCAN_RESULT:
            handle_wifi_scan_result(info);
            break;
         case NET_EVENT_WIFI_SCAN_DONE:
            handle_wifi_scan_done(info);
            break;

         default:
            break;
      }
   }

   void_t try_connect() {
      auto ssid = hei::settings::wifi::ssid();
      auto password = hei::settings::wifi::password();
      auto security = hei::settings::wifi::security();
      if (!ssid || !password || !security) {
         return unexpected(EINVAL);
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
         return unexpected(err);
      }

      auto events =
         k_event_wait(&state_, event::connected | event::error, false, K_SECONDS(CONFIG_APP_WIFI_CONNECTION_TIMEOUT));
      if ((events & event::connected) != event::connected) {
         LOG_ERR("Connection error");
         return unexpected(ENETUNREACH);
      }

      LOG_DBG("Connection started, waiting for IP");
      net_dhcpv4_start(iface_);

      events = k_event_wait(&state_, event::l4_connected | event::error, false,
                            K_SECONDS(CONFIG_APP_WIFI_CONNECTION_TIMEOUT));
      if ((events & event::l4_connected) != event::l4_connected) {
         LOG_ERR("Error getting IPv4");
         return unexpected(ENETUNREACH);
      }

      LOG_INF("Connected to \"%s\" network", ssid->data());
      print_ipv4_addresses();
      return {};
   }

   bool connect() {
      if (!hei::settings::configured()) {
         return false;
      }

      LOG_DBG("Application is fully configured, trying to connect");
      for (int i = 0; i < CONFIG_APP_WIFI_CONNECTION_ATTEMPTS; ++i) {
         clear_events();

         auto res = try_connect();
         if (res) {
            return true;
         }

         LOG_ERR("Wi-Fi connection attempt #%d error: %s", i, res.error().message().c_str());
         k_sleep(K_SECONDS(5));
      }

      return false;
   }

   void_t host() {
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
         return unexpected(err);
      }

      LOG_DBG("Waiting for L4");
      return configure_ap_ip().and_then([&]() -> void_t {
         auto events = k_event_wait(&state_, event::l4_connected | event::error, false,
                                    K_SECONDS(CONFIG_APP_WIFI_CONNECTION_TIMEOUT));
         if ((events & event::l4_connected) != event::l4_connected) {
            LOG_ERR("AP configuration failed: %d", events);
            return unexpected(ENETUNREACH);
         }

         LOG_INF("Configuring host network");
         return configure_dhcp_server().and_then([&] {
            print_ipv4_addresses();
            return hei::dns::server::start();
         });
      });
   }

   void_t configure_ap_ip() {
      return from_string("192.168.0.1").and_then([&](auto addr) -> void_t {
         if (!net_if_ipv4_addr_add(iface_, &addr, NET_ADDR_MANUAL, 0)) {
            LOG_ERR("Set manual IP failed");
            return unexpected(ENETDOWN);
         }

         return from_string("255.255.255.0").and_then([&](auto mask) -> void_t {
            if (!net_if_ipv4_set_netmask_by_addr(iface_, &addr, &mask)) {
               LOG_ERR("Set netmask failed");
               return unexpected(ENETDOWN);
            }
            return {};
         });
      });
   }

   void_t configure_dhcp_server() {
      return from_string("192.168.0.100").and_then([&](auto addr) -> void_t {
         if (auto err = net_dhcpv4_server_start(iface_, &addr)) {
            LOG_ERR("DHCP server start error: %d", err);
            return unexpected(err);
         }
         return {};
      });
   }

   void start_network_scan() {
      wifi_scan_params params = {
         .scan_type = WIFI_SCAN_TYPE_PASSIVE,
         .dwell_time_passive = CONFIG_APP_NETWORK_SCAN_DURATION * MSEC_PER_SEC,
         .max_bss_cnt = static_cast<std::uint16_t>(networks_.size()),
      };

      if (auto err = net_mgmt(NET_REQUEST_WIFI_SCAN, iface_, &params, sizeof(params))) {
         // This isn't critical, so we are not doing anything here
         LOG_ERR("Error starting a network scan: %d", err);
         return;
      }
   }

private:
   k_event state_;
   net_if *iface_;

   net_event_cb_holder wifi_;
   net_event_cb_holder l4_;

   bool is_hosting_{false};

   static wifi_manager *instance_;

   hei::wifi::mac_addr_t mac_addr_{0};

   zephyr::mutex scan_mutex_{};
   k_timer periodic_network_scan_timer_{};
   k_work periodic_network_scan_work_{};
   std::size_t num_networks_{0};
   hei::wifi::network_list_t networks_{};
};

wifi_manager *wifi_manager::instance_ = nullptr;

} // namespace

namespace hei::wifi {

void_t setup() {
   if (wifi_manager::get()) {
      LOG_ERR("Wi-Fi already initialized");
      return unexpected(EINVAL);
   }

   auto iface = net_if_get_default();
   if (!iface) {
      LOG_ERR("No network interface available");
      return unexpected(ENODEV);
   }

   static wifi_manager manager{*iface};
   return manager.start();
}

bool is_hosting() {
   return wifi_manager::get()->is_hosting();
}

std::string_view mac_address() {
   return wifi_manager::get()->get_mac();
}

void with_network_list(const network_list_handler_t &cb) {
   wifi_manager::get()->with_networks(cb);
}

////////////////////////////////////////////////////////////////////////////////
/// Class: network
////////////////////////////////////////////////////////////////////////////////
network::network(const wifi_scan_result &scan)
   : ssid{0}
   , mac{0}
   , channel{scan.channel}
   , rssi{scan.rssi}
   , band{static_cast<wifi_frequency_bands>(scan.band)}
   , security{scan.security}
   , mfp{scan.mfp} {
   using namespace std;
   copy(begin(scan.ssid), end(scan.ssid), begin(ssid));
   ssid[scan.ssid_length] = 0;

   mac_to_str(scan.mac, scan.mac_length, mac);
}

bool network::operator<(const network &o) const {
   // Put the lowers-RSSI values first
   return rssi > o.rssi;
}

bool network::operator==(const network &o) const {
   using namespace std;
   return ssid == o.ssid;
}

} // namespace hei::wifi
