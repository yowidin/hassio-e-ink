/**
 * @file   server.cpp
 * @author Dennis Sitelew
 * @date   Aug. 05, 2024
 */

#include <hei/server.hpp>

#include <zephyr/logging/log.h>
#include <zephyr/net/http/server.h>
#include <zephyr/net/http/service.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <string_view>

LOG_MODULE_REGISTER(hei_http, CONFIG_APP_LOG_LEVEL);

namespace {

constexpr std::uint8_t index_html_gz[] = {
#include <hei/http/static/index.html.gz.inc>
};
constexpr std::size_t index_html_gz_size = sizeof(index_html_gz);

constexpr std::uint16_t http_server_port = 80;

std::uint8_t status_rx_buffer[1024];

class endpoint {
protected:
   static constexpr std::size_t target_buffer_size = 512;
   using array_t = std::array<std::uint8_t, target_buffer_size>;

public:
   auto &get_buffer() { return data_; }

   int handle_chunk(http_data_status status, http_method method, std::uint8_t *buffer, std::size_t length) {
      if (status == HTTP_SERVER_DATA_ABORTED) {
         LOG_DBG("Transaction aborted after %zd bytes.", size_);
         size_ = 0;
         return 0;
      }

      size_ += length;

      if (status == HTTP_SERVER_DATA_FINAL) {
         LOG_DBG("All data received (%zd bytes).", size_);

         auto result = handle_request(method);
         size_ = 0;
         return static_cast<int>(result);
      }

      return static_cast<int>(length);
   }

protected:
   virtual std::size_t handle_request(http_method method) = 0;

protected:
   array_t data_{};
   std::size_t size_{0};
};

class status_endpoint : public endpoint {
public:
   virtual std::size_t handle_request(http_method method) override {
      auto response = std::string_view{"{\"foo\": 42}"};
      using namespace std;
      copy(begin(response), end(response), begin(data_));
      return response.size();
   }
};

class available_networks_endpoint : public endpoint {
public:
   virtual std::size_t handle_request(http_method method) override {
      auto response = std::string_view{"{}"};
      using namespace std;
      copy(begin(response), end(response), begin(data_));
      return response.size();
   }
};

class update_config_endpoint : public endpoint {
public:
   virtual std::size_t handle_request(http_method method) override {
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

struct http_resource_detail_static index_html_gz_resource_detail = {
   .common =
      {
         .bitmask_of_supported_http_methods = BIT(HTTP_GET),
         .type = HTTP_RESOURCE_TYPE_STATIC,
         .content_encoding = "gzip",
         .content_type = "text/html",
      },
   .static_data = index_html_gz,
   .static_data_len = index_html_gz_size,
};

HTTP_RESOURCE_DEFINE(index_html_gz_resource, http_server_service, "/", &index_html_gz_resource_detail);

static int dyn_handler(http_client_ctx *client,
                       http_data_status data_status,
                       std::uint8_t *buffer,
                       std::size_t len,
                       void *user_data) {
   ARG_UNUSED(user_data);

   auto path = reinterpret_cast<const char *>(client->url_buffer);
   LOG_DBG("Got request: %s", path);

   auto ep = endpoint_from_url(std::string_view{path});
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

namespace hei::server {

void start() {
   http_server_start();
}

} // namespace hei::server