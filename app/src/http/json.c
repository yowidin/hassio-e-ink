/**
 * @file   json.c
 * @author Dennis Sitelew
 * @date   Sep. 13, 2024
 */

#include <hei/http/json.h>

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(http_json, CONFIG_APP_LOG_LEVEL);

const char *hei_http_json_decoding_result_to_response_message(hei_http_json_decoding_result_t res) {
   switch (res) {
      case hei_http_jdr_success:
         return "ok";
      case hei_http_jdr_decoding_error:
         return "bad JSON";
      case hei_http_jdr_invalid_object:
         return "unexpected encoding";
      default:
         LOG_ERR("Unexpected result value: %d", (int)res);
         return "internal server error";
   }
}
