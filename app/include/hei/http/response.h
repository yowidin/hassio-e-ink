/**
 * @file   response.h
 * @author Dennis Sitelew
 * @date   Sep. 13, 2024
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include <stdbool.h>
#include <stddef.h>

typedef struct hei_http_response {
   const char *result;
   const char *message;
} hei_http_response_t;

bool hei_http_response_to_json(const hei_http_response_t *obj, char *target, size_t target_size);

#ifdef __cplusplus
}
#endif /* __cplusplus */
