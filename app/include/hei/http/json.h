/**
 * @file   json.h
 * @author Dennis Sitelew
 * @date   Sep. 13, 2024
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef enum hei_http_json_decoding_result {
   hei_http_jdr_success = 0,
   hei_http_jdr_decoding_error,
   hei_http_jdr_invalid_object,
} hei_http_json_decoding_result_t;

const char *hei_http_json_decoding_result_to_response_message(hei_http_json_decoding_result_t res);

#ifdef __cplusplus
}
#endif /* __cplusplus */