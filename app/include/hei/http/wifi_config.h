/**
 * @file   wifi_config.h
 * @author Dennis Sitelew
 * @date   Sep. 13, 2024
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include <hei/http/json.h>

#include <stdbool.h>
#include <stddef.h>

typedef struct hei_http_wifi_config {
   const char *name;
   const char *password;
   const char *security;
} hei_http_wifi_config_t;

/**
 * Try decoding the Wi-Fi configuration from JSON.
 */
hei_http_json_decoding_result_t hei_http_wifi_config_from_json(char *src, size_t src_size, hei_http_wifi_config_t *cfg);

#ifdef __cplusplus
}
#endif /* __cplusplus */
