/**
 * @file   status.c
 * @author Dennis Sitelew
 * @date   Aug. 24, 2024
 */

#include <hei/http/status.h>

#include <zephyr/data/json.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(http_status, CONFIG_APP_LOG_LEVEL);

static struct json_obj_descr wifi_desc[] = {
   JSON_OBJ_DESCR_PRIM(hei_http_status_wifi_config_t, ssid, JSON_TOK_STRING),
   JSON_OBJ_DESCR_PRIM(hei_http_status_wifi_config_t, has_ssid, JSON_TOK_TRUE),
   JSON_OBJ_DESCR_PRIM(hei_http_status_wifi_config_t, security, JSON_TOK_STRING),
   JSON_OBJ_DESCR_PRIM(hei_http_status_wifi_config_t, has_security, JSON_TOK_TRUE),
};

static struct json_obj_descr image_server_desc[] = {
   JSON_OBJ_DESCR_PRIM(hei_http_status_image_server_config_t, address, JSON_TOK_STRING),
   JSON_OBJ_DESCR_PRIM(hei_http_status_image_server_config_t, has_address, JSON_TOK_TRUE),
   JSON_OBJ_DESCR_PRIM(hei_http_status_image_server_config_t, port, JSON_TOK_NUMBER),
   JSON_OBJ_DESCR_PRIM(hei_http_status_image_server_config_t, has_port, JSON_TOK_TRUE),
   JSON_OBJ_DESCR_PRIM(hei_http_status_image_server_config_t, update_interval, JSON_TOK_NUMBER),
   JSON_OBJ_DESCR_PRIM(hei_http_status_image_server_config_t, has_update_interval, JSON_TOK_TRUE),
};

static struct json_obj_descr status_desc[] = {
   JSON_OBJ_DESCR_PRIM(hei_http_status_t, mac_address, JSON_TOK_STRING),
   JSON_OBJ_DESCR_PRIM(hei_http_status_t, is_hosting, JSON_TOK_TRUE),
   JSON_OBJ_DESCR_OBJECT(hei_http_status_t, wifi, wifi_desc),
   JSON_OBJ_DESCR_OBJECT(hei_http_status_t, image_server, image_server_desc),
};

bool hei_http_status_to_json(const hei_http_status_t *status, char *target, size_t target_size) {
   const ssize_t expected_size = json_calc_encoded_len(status_desc, ARRAY_SIZE(status_desc), status);
   if (expected_size > target_size) {
      LOG_ERR("Not enough space for status: %zd vs %zd", expected_size, target_size);
      return false;
   }

   const int res = json_obj_encode_buf(status_desc, ARRAY_SIZE(status_desc), status, target, target_size);
   if (res < 0) {
      LOG_ERR("Error encoding status: %d", res);
      return false;
   }

   return true;
}
