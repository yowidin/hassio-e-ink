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

LOG_MODULE_REGISTER(hei_http, CONFIG_APP_LOG_LEVEL);

namespace {

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

class endpoint {
protected:
   static constexpr std::size_t target_buffer_size = 1024;
   using array_t = std::array<std::uint8_t, target_buffer_size>;

public:
   virtual ~endpoint() = default;

public:
   auto &get_buffer() { return data_; }

   int handle_chunk(const http_data_status status,
                    const http_method method,
                    const std::uint8_t *buffer,
                    const std::size_t length) {
      ARG_UNUSED(buffer);

      if (status == HTTP_SERVER_DATA_ABORTED) {
         LOG_DBG("Transaction aborted after %zd bytes.", size_);
         size_ = 0;
         return 0;
      }

      size_ += length;

      if (status == HTTP_SERVER_DATA_FINAL) {
         LOG_DBG("All data received (%zd bytes).", size_);

         const auto result = handle_request(method);
         size_ = 0;
         return static_cast<int>(result);
      }

      return static_cast<int>(length);
   }

protected:
   virtual std::size_t handle_request(http_method method) = 0;

   auto char_data() { return reinterpret_cast<char *>(data_.data()); }

   [[noreturn]] static void throw_system_error(std::errc code) { throw std::system_error(std::make_error_code(code)); }

   [[noreturn]] static void throw_system_error(int code) {
      throw std::system_error(std::make_error_code(std::errc{code}));
   }

protected:
   array_t data_{};
   std::size_t size_{0};
};

class status_endpoint : public endpoint {
public:
   std::size_t handle_request(http_method method) override {
      const auto is_hosting = hei::wifi::is_hosting();
      const auto mac = hei::wifi::mac_address();
      const hei_http_status_t obj = {
         .mac_address = mac.data(),
         .is_hosting = is_hosting,
      };

      if (auto err = hei_http_status_to_json(&obj, char_data(), data_.size())) {
         LOG_ERR("Error encoding status: %d", err);
         return err;
      }

      return std::strlen(char_data());
   }
};

class available_networks_endpoint : public endpoint {
private:
public:
   std::size_t handle_request(http_method method) override {
      hei::wifi::with_nwtwork_list([this](const auto &networks, auto count) {
         networks_.num_networks = count;
         for (std::size_t i = 0; i < count; ++i) {
            auto &target = networks_.networks[i];
            const auto &src = networks[i];

            target.ssid = reinterpret_cast<const char *>(src.ssid.data());
            target.mac = src.mac.data();
            target.channel = src.channel;
            target.rssi = static_cast<std::int32_t>(src.rssi);

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

            switch (src.security) {
               case WIFI_SECURITY_TYPE_NONE:
                  target.security = "OPEN";
                  break;
               case WIFI_SECURITY_TYPE_WEP:
                  target.security = "WEP";
                  break;
               case WIFI_SECURITY_TYPE_WPA_PSK:
                  target.security = "WPA-PSK";
                  break;
               case WIFI_SECURITY_TYPE_PSK:
                  target.security = "WPA2-PSK";
                  break;
               case WIFI_SECURITY_TYPE_PSK_SHA256:
                  target.security = "WPA2-PSK-SHA256";
                  break;
               case WIFI_SECURITY_TYPE_SAE:
                  target.security = "WPA3-SAE";
                  break;
               case WIFI_SECURITY_TYPE_WAPI:
                  target.security = "WAPI";
                  break;
               case WIFI_SECURITY_TYPE_EAP:
                  target.security = "EAP";
                  break;
               case WIFI_SECURITY_TYPE_UNKNOWN:
               default:
                  target.security = "[unknown]";
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

      if (auto err = hei_http_network_list_to_json(&networks_, char_data(), data_.size())) {
         LOG_ERR("Error encoding network list: %d", err);
         return err;
      }

      return std::strlen(char_data());
   }

private:
   hei_http_network_list_t networks_{};
};

class update_config_endpoint : public endpoint {
public:
   std::size_t handle_request(http_method method) override {
      auto response = std::string_view{"{}"};
      using namespace std;
      copy(begin(response), end(response), begin(data_));
      return response.size();
   }
};

status_endpoint status{};
available_networks_endpoint available_networks{};
update_config_endpoint update_config{};

struct named_endpoint {
   std::string_view path;
   endpoint *ep;
};

std::array endpoints = {
   named_endpoint{"/status", &status},
   named_endpoint{"/networks", &available_networks},
   named_endpoint{"/config", &update_config},
};

endpoint *endpoint_from_url(std::string_view path) {
   for (auto &ep : endpoints) {
      if (path.starts_with(ep.path)) {
         return ep.ep;
      }
   }
   return nullptr;
}

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
   ARG_UNUSED(user_data);

   const auto path = reinterpret_cast<const char *>(client->url_buffer);
   LOG_DBG("Got request: %s", path);

   const auto ep = endpoint_from_url(std::string_view{path});
   if (ep) {
      return ep->handle_chunk(data_status, client->method, buffer, len);
   }

   LOG_WRN("Unexpected endpoint: %s", path);
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
      .data_buffer = endpoint.get_buffer().data(),                        \
      .data_buffer_len = available_networks.get_buffer().size(),          \
      .user_data = nullptr,                                               \
   };                                                                     \
   HTTP_RESOURCE_DEFINE(CONCAT(name, _resource), http_server_service, path, &CONCAT(name, _resource_detail))

DYNAMIC_ENDPOINT(http_status, "/status", status);
DYNAMIC_ENDPOINT(http_networks, "/networks", available_networks);
DYNAMIC_ENDPOINT(http_config, "/config", update_config);

} // extern "C"

namespace hei::http::server {

void start() {
   http_server_start();
}

} // namespace hei::http::server