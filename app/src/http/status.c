/**
 * @file   status.c
 * @author Dennis Sitelew
 * @date   Aug. 24, 2024
 */

#include <hei/http/status.h>

#include <zephyr/data/json.h>

static struct json_obj_descr status_desc[] = {
   JSON_OBJ_DESCR_PRIM(hei_http_status_t, mac_address, JSON_TOK_STRING),
   JSON_OBJ_DESCR_PRIM(hei_http_status_t, is_hosting, JSON_TOK_TRUE),
};

int hei_http_status_to_json(const hei_http_status_t *status, char *target, size_t target_size) {
   return json_obj_encode_buf(status_desc, ARRAY_SIZE(status_desc), status, target, target_size);
}
