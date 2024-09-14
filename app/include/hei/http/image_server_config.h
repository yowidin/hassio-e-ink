/**
 * @file   image_server_config.h
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

typedef struct hei_http_image_server_config {
   const char *address;
   int port;
   int interval;
} hei_http_image_server_config_t;

/**
 * Try decoding the image server configuration from JSON.
 */
hei_http_json_decoding_result_t hei_http_image_server_config_from_json(char *src,
                                                                       size_t src_size,
                                                                       hei_http_image_server_config_t *cfg);

#ifdef __cplusplus
}
#endif /* __cplusplus */
