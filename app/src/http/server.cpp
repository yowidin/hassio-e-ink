/**
 * @file   server.cpp
 * @author Dennis Sitelew
 * @date   Aug. 05, 2024
 */

#include <hei/http/network_list.h>
#include <hei/http/status.h>
#include <hei/http/server.hpp>

#include <hei/wifi.hpp>

#include <zephyr/logging/log.h>
#include <zephyr/net/http/server.h>
#include <zephyr/net/http/service.h>

#include <zephyr/data/json.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <system_error>
#include <unordered_map>

LOG_MODULE_REGISTER(hei_http, CONFIG_APP_LOG_LEVEL);

namespace {

const char *to_string(wifi_security_type type) {
   switch (type) {
      case WIFI_SECURITY_TYPE_NONE:
         return "OPEN";
      case WIFI_SECURITY_TYPE_WEP:
         return "WEP";
      case WIFI_SECURITY_TYPE_WPA_PSK:
         return "WPA-PSK";
      case WIFI_SECURITY_TYPE_PSK:
         return "WPA2-PSK";
      case WIFI_SECURITY_TYPE_PSK_SHA256:
         return "WPA2-PSK-SHA256";
      case WIFI_SECURITY_TYPE_SAE:
         return "WPA3-SAE";
      case WIFI_SECURITY_TYPE_WAPI:
         return "WAPI";
      case WIFI_SECURITY_TYPE_EAP:
         return "EAP";
      case WIFI_SECURITY_TYPE_UNKNOWN:
      default:
         return "[unknown]";
   }
}

template <typename T>
T from_string(const char *);

template <>
wifi_security_type from_string<wifi_security_type>(const char *text) {
   const static std::unordered_map<std::string, wifi_security_type> mapping = {
      {"OPEN", WIFI_SECURITY_TYPE_NONE},
      {"WEP", WIFI_SECURITY_TYPE_WEP},
      {"WPA-PSK", WIFI_SECURITY_TYPE_WPA_PSK},
      {"WPA2-PSK", WIFI_SECURITY_TYPE_PSK},
      {"WPA2-PSK-SHA256", WIFI_SECURITY_TYPE_PSK_SHA256},
      {"WPA3-SAE", WIFI_SECURITY_TYPE_SAE},
      {"WAPI", WIFI_SECURITY_TYPE_WAPI},
      {"EAP", WIFI_SECURITY_TYPE_EAP},
      {"[unknown]", WIFI_SECURITY_TYPE_UNKNOWN},
   };

   auto it = mapping.find(text);
   if (it == mapping.end()) {
      return WIFI_SECURITY_TYPE_UNKNOWN;
   }

   return it->second;
}

constexpr std::uint8_t index_html[] = {
#include <hei/http/static/index.html.gz.inc>
};

constexpr std::uint8_t bootstrap_min_css[] = {
#include <hei/http/static/bootstrap.min.css.gz.inc>
};

constexpr std::uint8_t bootstrap_bundle_min_js[] = {
#include <hei/http/static/bootstrap.bundle.min.js.gz.inc>
};

constexpr std::uint16_t http_server_port = 80;

class endpoint_base {
public:
   virtual ~endpoint_base() = default;

public:
   virtual int handle_chunk(http_data_status status,
                            http_method method,
                            const std::uint8_t *buffer,
                            std::size_t length) = 0;
};

template <std::size_t PayloadSize = 1024, std::size_t InputBufferSize = 32>
class endpoint : public endpoint_base {
protected:
   static constexpr std::size_t max_payload_size = PayloadSize;
   using payload_array_t = std::array<std::uint8_t, max_payload_size>;

   static constexpr std::size_t input_buffer_size = InputBufferSize;
   using input_array_t = std::array<std::uint8_t, input_buffer_size>;

public:
   [[nodiscard]] auto http_buffer() { return input_buffer_.data(); }
   [[nodiscard]] auto http_buffer_size() const { return input_buffer_size; }

   int handle_chunk(const http_data_status status,
                    const http_method method,
                    const std::uint8_t *buffer,
                    const std::size_t length) override {
      ARG_UNUSED(buffer);

      if (status == HTTP_SERVER_DATA_ABORTED) {
         LOG_DBG("Transaction aborted after %zd bytes.", payload_size_);
         payload_size_ = 0;
         return 0;
      }

      if (length + payload_size_ > max_payload_size) {
         LOG_DBG("Payload too big: %zd vs %zd (cannot store %zd more bytes)", payload_size_, max_payload_size, length);
         payload_size_ = 0;
         return -413; // Request entity too large
      }

      std::copy(buffer, buffer + length, payload_buffer_.begin() + payload_size_);
      payload_size_ += length;

      if (status == HTTP_SERVER_DATA_FINAL) {
         LOG_DBG("All data received (%zd bytes).", payload_size_);

         const auto result = handle_request(method);
         payload_size_ = 0;
         return result;
      }

      // We don't have anything to send in response - return 0
      return 0;
   }

protected:
   virtual int handle_request(http_method method) = 0;

   [[nodiscard]] auto char_payload() { return reinterpret_cast<char *>(payload_buffer_.data()); }
   [[nodiscard]] auto payload_size() const { return payload_size_; }

protected:
   //! HTTP payload data will be written into this array with each HTTP server handler call.
   input_array_t input_buffer_{};

   //! Final payload buffer (containing complete request payload).
   //! Even for a small payload, it may arrive split into chunks (e.g. if the header size was such that the whole HTTP
   //	 request exceeds the size of the client buffer).
   payload_array_t payload_buffer_{};
   std::size_t payload_size_{0};
};

class status_endpoint : public endpoint<128> {
public:
   int handle_request(http_method method) override {
      const auto is_hosting = hei::wifi::is_hosting();
      const auto mac = hei::wifi::mac_address();
      const hei_http_status_t obj = {
         .mac_address = mac.data(),
         .is_hosting = is_hosting,
      };

      if (!hei_http_status_to_json(&obj, char_payload(), max_payload_size)) {
         return -500;
      }

      return static_cast<int>(std::strlen(char_payload()));
   }
};

class available_networks_endpoint : public endpoint<1024> {
private:
public:
   int handle_request(http_method method) override {
      hei::wifi::with_nwtwork_list([this](const auto &networks, auto count) {
         networks_.num_networks = count;
         for (std::size_t i = 0; i < count; ++i) {
            auto &target = networks_.networks[i];
            const auto &src = networks[i];

            target.ssid = reinterpret_cast<const char *>(src.ssid.data());
            target.mac = src.mac.data();
            target.channel = src.channel;
            target.rssi = static_cast<std::int32_t>(src.rssi); // NOLINT(*-str34-c)
            target.security = to_string(src.security);

            switch (src.band) {
               case WIFI_FREQ_BAND_2_4_GHZ:
                  target.band = "2.4 GHz";
                  break;
               case WIFI_FREQ_BAND_5_GHZ:
                  target.band = "5 GHz";
                  break;
               case WIFI_FREQ_BAND_6_GHZ:
                  target.band = "6 GHz";
                  break;
               default:
                  target.band = "[unknown]";
                  break;
            }

            switch (src.mfp) {
               case WIFI_MFP_DISABLE:
                  target.mfp = "Disabled";
                  break;
               case WIFI_MFP_OPTIONAL:
                  target.mfp = "Optional";
                  break;
               case WIFI_MFP_REQUIRED:
                  target.mfp = "Required";
                  break;
               default:
                  target.mfp = "[unknown]";
                  break;
            }
         }
      });

      if (!hei_http_network_list_to_json(&networks_, char_payload(), max_payload_size)) {
         return -500;
      }

      return static_cast<int>(std::strlen(char_payload()));
   }

private:
   hei_http_network_list_t networks_{};
};

class update_wifi_config_endpoint : public endpoint<256> {
public:
   int handle_request(http_method method) override {
      auto response = std::string_view{"{}"};
      using namespace std;
      copy(begin(response), end(response), char_payload());
      // TODO
      return static_cast<int>(response.size());
   }
};

class update_image_server_config_endpoint : public endpoint<512> {
public:
   int handle_request(http_method method) override {
      auto response = std::string_view{"{}"};
      using namespace std;
      copy(begin(response), end(response), char_payload());
      // TODO
      return static_cast<int>(response.size());
   }
};

status_endpoint get_status{};
available_networks_endpoint get_available_networks{};
update_wifi_config_endpoint update_wifi_config{};
update_image_server_config_endpoint update_image_server_config{};

} // namespace

extern "C" {

HTTP_SERVICE_DEFINE(http_server_service, "0.0.0.0", &http_server_port, 1, 10, NULL);

#define STATIC_RESOURCE(path, resource_type, resource)          \
   http_resource_detail_static CONCAT(resource, _detail) = {    \
      .common =                                                 \
         {                                                      \
            .bitmask_of_supported_http_methods = BIT(HTTP_GET), \
            .type = HTTP_RESOURCE_TYPE_STATIC,                  \
            .content_encoding = "gzip",                         \
            .content_type = resource_type,                      \
         },                                                     \
      .static_data = resource,                                  \
      .static_data_len = sizeof(resource),                      \
   };                                                           \
   constexpr HTTP_RESOURCE_DEFINE(CONCAT(resource, _resource), http_server_service, path, &CONCAT(resource, _detail))

STATIC_RESOURCE("/", "text/html", index_html);
STATIC_RESOURCE("/bootstrap.min.css", "text/css", bootstrap_min_css);
STATIC_RESOURCE("/bootstrap.bundle.min.js", "text/javascript", bootstrap_bundle_min_js);

static int dyn_handler(http_client_ctx *client,
                       http_data_status data_status,
                       std::uint8_t *buffer,
                       std::size_t len,
                       void *user_data) {
   LOG_DBG("Handling request: %s", client->url_buffer);

   const auto ep = reinterpret_cast<endpoint_base *>(user_data);
   if (ep) {
      return ep->handle_chunk(data_status, client->method, buffer, len);
   }

   LOG_WRN("Unexpected endpoint: %s", client->url_buffer);
   return -404;
}

#define DYNAMIC_ENDPOINT(name, path, endpoint)                            \
   struct http_resource_detail_dynamic CONCAT(name, _resource_detail) = { \
      .common =                                                           \
         {                                                                \
            .bitmask_of_supported_http_methods = BIT(HTTP_POST),          \
            .type = HTTP_RESOURCE_TYPE_DYNAMIC,                           \
         },                                                               \
      .cb = dyn_handler,                                                  \
      .data_buffer = endpoint.http_buffer(),                              \
      .data_buffer_len = endpoint.http_buffer_size(),                     \
      .user_data = static_cast<endpoint_base *>(&endpoint),               \
   };                                                                     \
   HTTP_RESOURCE_DEFINE(CONCAT(name, _resource), http_server_service, path, &CONCAT(name, _resource_detail))

DYNAMIC_ENDPOINT(http_status, "/status", get_status);
DYNAMIC_ENDPOINT(http_networks, "/networks", get_available_networks);
DYNAMIC_ENDPOINT(http_wifi_config, "/wifi-config", update_wifi_config);
DYNAMIC_ENDPOINT(http_image_server_config, "/image-server-config", update_image_server_config);

} // extern "C"

namespace hei::http::server {

void start() {
   http_server_start();
}

} // namespace hei::http::server