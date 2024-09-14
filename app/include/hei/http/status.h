/**
 * @file   status.h
 * @author Dennis Sitelew
 * @date   Aug. 24, 2024
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct hei_http_status_wifi_config {
   const char *ssid;
   bool has_ssid;

   const char *security;
   bool has_security;
} hei_http_status_wifi_config_t;

typedef struct hei_http_status_image_server_config {
   const char *address;
   bool has_address;

   int port;
   bool has_port;

   int update_interval;
   bool has_update_interval;
} hei_http_status_image_server_config_t;

typedef struct hei_http_status {
   const char *mac_address;
   bool is_hosting;

   hei_http_status_wifi_config_t wifi;
   hei_http_status_image_server_config_t image_server;
} hei_http_status_t;

bool hei_http_status_to_json(const hei_http_status_t *status, char *target, size_t target_size);

#ifdef __cplusplus
}
#endif /* __cplusplus */