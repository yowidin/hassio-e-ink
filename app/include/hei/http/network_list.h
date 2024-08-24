/**
 * @file   network_list.h
 * @author Dennis Sitelew
 * @date   Aug. 24, 2024
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include <stddef.h>
#include <stdint.h>

#include <autoconf.h>

typedef struct hei_http_network {
   const char *ssid;
   const char *mac;
   int channel;
   int rssi;
   const char *band;
   const char *security;
   const char *mfp;
} hei_http_network_t;

typedef struct hei_http_network_list {
   hei_http_network_t networks[CONFIG_APP_NETWORK_SCAN_MAX_RESULTS];
   size_t num_networks;
} hei_http_network_list_t;

int hei_http_network_list_to_json(hei_http_network_list_t *networks, char *target, size_t target_size);

#ifdef __cplusplus
}
#endif /* __cplusplus */
