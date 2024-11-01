/**
 * @file   wifi.hpp
 * @author Dennis Sitelew
 * @date   Aug. 02, 2024
 */

#pragma once

#include <hei/common.hpp>

#include <zephyr/net/wifi.h>
#include <zephyr/net/wifi_mgmt.h>

#include <string_view>
#include <array>
#include <span>
#include <functional>

namespace hei::wifi {

inline constexpr std::size_t MAC_ADDR_LEN = 18; // "XX:XX:XX:XX:XX:XX"

using mac_addr_t = std::array<char, hei::wifi::MAC_ADDR_LEN + 1>;
using ssid_t = std::array<std::uint8_t, WIFI_SSID_MAX_LEN + 1>;

struct network {
   explicit network(const wifi_scan_result &scan);

   network() = default;
   network(const network &o) = default;
   network &operator=(const network &o) = default;

   bool operator<(const network &o) const;
   bool operator==(const network &o) const;
   bool operator!=(const network &o) const {
      return !(*this == o);
   }

   ssid_t ssid;
   mac_addr_t mac;
   std::uint8_t channel;
   std::int8_t rssi;
   wifi_frequency_bands band;
   wifi_security_type security;
   wifi_mfp_options mfp;
};

using network_list_t = std::array<hei::wifi::network, CONFIG_APP_NETWORK_SCAN_MAX_RESULTS>;
using network_list_handler_t = std::function<void(const hei::wifi::network_list_t &networks, std::size_t num_active)>;

/**
 * Ensure there is an active network configuration (either connected to a network, or an AP is configured to accept
 * clients).
 */
void_t setup();

//! @return true if hosting an access point, false otherwise (client)
bool is_hosting();

//! Get our own MAC address as a string view
std::string_view mac_address();

//! Call the @ref cb handler with the current list of discovered networks.
//! Use this roundabout way to avoid fiddling with the mutex or making a copy.
void with_network_list(const network_list_handler_t &cb);

} // namespace hei::wifi