/**
 * @file   network_list.c
 * @author Dennis Sitelew
 * @date   Aug. 24, 2024
 */

#include <hei/http/network_list.h>

#include <zephyr/data/json.h>
#include <errno.h>

// clang-format off
static struct json_obj_descr network_desc[] = {
   JSON_OBJ_DESCR_PRIM(hei_http_network_t, ssid, JSON_TOK_STRING),
   JSON_OBJ_DESCR_PRIM(hei_http_network_t, mac, JSON_TOK_STRING),
   JSON_OBJ_DESCR_PRIM(hei_http_network_t, channel, JSON_TOK_NUMBER),
   JSON_OBJ_DESCR_PRIM(hei_http_network_t, rssi, JSON_TOK_NUMBER),
   JSON_OBJ_DESCR_PRIM(hei_http_network_t, band, JSON_TOK_STRING),
   JSON_OBJ_DESCR_PRIM(hei_http_network_t, security, JSON_TOK_STRING),
   JSON_OBJ_DESCR_PRIM(hei_http_network_t, mfp, JSON_TOK_STRING),
};
// clang-format on

// clang-format off
static struct json_obj_descr network_list_desc[] = {
   JSON_OBJ_DESCR_OBJ_ARRAY(hei_http_network_list_t, networks, CONFIG_APP_NETWORK_SCAN_MAX_RESULTS, num_networks,
                            network_desc, ARRAY_SIZE(network_desc)),
};
// clang-format on

int hei_http_network_list_to_json(hei_http_network_list_t *networks, char *target, size_t target_size) {
   ssize_t size;

   // Reduce the number of networks until we either have enough space or there is nothing else left to skip
   while ((size = json_calc_encoded_len(network_list_desc, ARRAY_SIZE(network_list_desc), networks)) > target_size) {
      if (networks->num_networks <= 1) {
         // Not enough memory to hold even a single entry
         return -ENOMEM;
      }

      networks->num_networks -= 1;
   }
   return json_obj_encode_buf(network_list_desc, ARRAY_SIZE(network_list_desc), networks, target, target_size);
}

