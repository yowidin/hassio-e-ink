/**
 * @file   server.cpp
 * @author Dennis Sitelew
 * @date   Aug. 05, 2024
 */

#include <hei/server.hpp>

#include <zephyr/net/http/server.h>
#include <zephyr/net/http/service.h>

#include <cstdint>

namespace {

constexpr std::uint8_t index_html_gz[] = {
#include <hei/http/static/index.html.gz.inc>
};
constexpr std::size_t index_html_gz_size = sizeof(index_html_gz);

constexpr std::uint16_t http_server_port = 80;

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
   .static_data_len = sizeof(index_html_gz),
};

HTTP_RESOURCE_DEFINE(index_html_gz_resource, http_server_service, "/", &index_html_gz_resource_detail);
}

namespace hei::server {

void start() {
   http_server_start();
}

} // namespace hei::server