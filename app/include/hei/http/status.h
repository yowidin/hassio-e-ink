/**
 * @file   status.h
 * @author Dennis Sitelew
 * @date   Aug. 24, 2024
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include <stdbool.h>
#include <stddef.h>

typedef struct hei_http_status {
   const char *mac_address;
   bool is_hosting;
} hei_http_status_t;

int hei_http_status_to_json(const hei_http_status_t *status, char *target, size_t target_size);

#ifdef __cplusplus
}
#endif /* __cplusplus */