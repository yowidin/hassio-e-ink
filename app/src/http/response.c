/**
 * @file   response.c
 * @author Dennis Sitelew
 * @date   Sep. 13, 2024
 */

#include <hei/http/response.h>

#include <zephyr/data/json.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(http_response, CONFIG_APP_LOG_LEVEL);

static struct json_obj_descr json_description[] = {
   JSON_OBJ_DESCR_PRIM(hei_http_response_t, result, JSON_TOK_STRING),
   JSON_OBJ_DESCR_PRIM(hei_http_response_t, message, JSON_TOK_STRING),
};

bool hei_http_response_to_json(const hei_http_response_t *obj, char *target, size_t target_size) {
   const ssize_t expected_size = json_calc_encoded_len(json_description, ARRAY_SIZE(json_description), obj);
   if (expected_size > target_size) {
      LOG_ERR("Not enough space for response: %zd vs %zd", expected_size, target_size);
      return false;
   }

   const int res = json_obj_encode_buf(json_description, ARRAY_SIZE(json_description), obj, target, target_size);
   if (res < 0) {
      LOG_ERR("Error encoding response: %d", res);
      return false;
   }

   return true;
}
