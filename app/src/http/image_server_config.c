/**
 * @file   image_server_config.c
 * @author Dennis Sitelew
 * @date   Sep. 13, 2024
 */

#include <hei/http/image_server_config.h>

#include <zephyr/data/json.h>
#include <zephyr/logging/log.h>

#include <inttypes.h>

LOG_MODULE_REGISTER(http_image_server_config, CONFIG_APP_LOG_LEVEL);

static struct json_obj_descr json_description[] = {
   JSON_OBJ_DESCR_PRIM(hei_http_image_server_config_t, address, JSON_TOK_STRING),
   JSON_OBJ_DESCR_PRIM(hei_http_image_server_config_t, port, JSON_TOK_NUMBER),
   JSON_OBJ_DESCR_PRIM(hei_http_image_server_config_t, interval, JSON_TOK_NUMBER),
};

static const size_t json_description_size = ARRAY_SIZE(json_description);
static const int64_t expected_return_code = BIT_MASK(ARRAY_SIZE(json_description));

hei_http_json_decoding_result_t hei_http_image_server_config_from_json(char *src,
                                                                       size_t src_size,
                                                                       hei_http_image_server_config_t *cfg) {
   const int64_t ret = json_obj_parse(src, src_size, json_description, json_description_size, cfg);
   if (ret < 0) {
      LOG_WRN("JSON decoding error: %d", (int)ret);
      return hei_http_jdr_decoding_error;
   }

   if (ret != expected_return_code) {
      LOG_WRN("Unexpected JSON encoding: %" PRIx64 " vs %" PRIx64, ret, expected_return_code);
      return hei_http_jdr_invalid_object;
   }

   return hei_http_jdr_success;
}
