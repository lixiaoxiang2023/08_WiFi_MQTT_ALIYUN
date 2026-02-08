// json_processor.c
#include "json_processor.h"
#include <string.h>
#include <stdlib.h>

esp_err_t create_json_packet(json_packet_t *packet, cJSON **output) {
    cJSON *root = cJSON_CreateObject();
    if (!root) return ESP_FAIL;
    
    // 添加基础字段
    if (packet->device_id) {
        cJSON_AddStringToObject(root, "service_id", packet->device_id);
    }
    
    if (packet->timestamp > 0) {
        cJSON_AddNumberToObject(root, "event_time", packet->timestamp);
    }
    
    // 添加动态键值对
    for (size_t i = 0; i < packet->pair_count; i++) {
        kv_pair_t *pair = &packet->pairs[i];
        
        if (pair->is_null) {
            cJSON_AddNullToObject(root, pair->key);
        } else if (pair->is_string && pair->value_str) {
            cJSON_AddStringToObject(root, pair->key, pair->value_str);
        } else if (pair->is_number) {
            cJSON_AddNumberToObject(root, pair->key, pair->value_num);
        } else if (pair->is_bool) {
            cJSON_AddBoolToObject(root, pair->key, pair->value_bool);
        } else if (pair->is_object && pair->value_obj) {
            cJSON_AddItemToObject(root, pair->key, pair->value_obj);
        } else if (pair->is_array && pair->value_arr) {
            cJSON_AddItemToObject(root, pair->key, pair->value_arr);
        }
    }
    
    *output = root;
    return ESP_OK;
}

esp_err_t parse_json_packet(const char *json_str, json_packet_t *packet) {
    cJSON *root = cJSON_Parse(json_str);
    if (!root) return ESP_FAIL;
    
    // 解析基础字段
    cJSON *device_id = cJSON_GetObjectItem(root, "device_id");
    if (device_id && cJSON_IsString(device_id)) {
        packet->device_id = strdup(device_id->valuestring);
    }
    
    cJSON *timestamp = cJSON_GetObjectItem(root, "timestamp");
    if (timestamp && cJSON_IsNumber(timestamp)) {
        packet->timestamp = (uint32_t)timestamp->valuedouble;
    }
    
    // 解析其他键值对
    cJSON *item = root->child;
    while (item) {
        // 动态添加pair
        // 这里简化处理，实际应用中需要动态扩展pairs数组
        // ...
        item = item->next;
    }
    
    cJSON_Delete(root);
    return ESP_OK;
}

static char *dup_json_string(const cJSON *item)
{
    if (!cJSON_IsString(item) || !item->valuestring)
        return NULL;

    size_t len = strlen(item->valuestring);

    // 防止异常超长字段攻击
    if (len > 2048)
        return NULL;

    char *buf = malloc(len + 1);
    if (!buf) return NULL;

    memcpy(buf, item->valuestring, len);
    buf[len] = '\0';

    return buf;
}

static void zero_out(download_url_info_t *out)
{
    if (out) memset(out, 0, sizeof(*out));
}

void free_download_url_info(download_url_info_t *info)
{
    if (!info) return;

    free(info->object_device_id);
    free(info->event_type);
    free(info->service_id);
    free(info->event_time);

    free(info->url);
    free(info->bucket_name);
    free(info->object_name);

    free(info->hash_code);

    zero_out(info);
}

static char *safe_dup_json_string(cJSON *item)
{
    if (!cJSON_IsString(item) || !item->valuestring)
        return NULL;

    size_t len = strlen(item->valuestring);
    if (len > 4096) return NULL;  // 防止异常大字段

    char *buf = malloc(len + 1);
    if (!buf) return NULL;

    memcpy(buf, item->valuestring, len);
    buf[len] = '\0';
    return buf;
}

bool parse_download_url_response(const char *json_str, download_url_info_t *out)
{
    if (!json_str || !out) return false;
    zero_out(out);

    cJSON *root = cJSON_Parse(json_str);
    if (!root) return false;

    bool ok = false;

    // object_device_id
    cJSON *object_device_id = cJSON_GetObjectItem(root, "object_device_id");
    out->object_device_id = safe_dup_json_string(object_device_id);
    if (!out->object_device_id) goto end;

    // services
    cJSON *services = cJSON_GetObjectItem(root, "services");
    if (!cJSON_IsArray(services)) goto end;

    cJSON *svc0 = NULL;
    cJSON *svc = NULL;

    cJSON_ArrayForEach(svc, services) {
        cJSON *sid = cJSON_GetObjectItem(svc, "service_id");
        if (cJSON_IsString(sid) &&
            strcmp(sid->valuestring, "$file_manager") == 0) {
            svc0 = svc;
            break;
        }
    }
    if (!svc0) goto end;

    // fields
    out->event_type = safe_dup_json_string(
        cJSON_GetObjectItem(svc0, "event_type"));
    out->service_id = safe_dup_json_string(
        cJSON_GetObjectItem(svc0, "service_id"));
    out->event_time = safe_dup_json_string(
        cJSON_GetObjectItem(svc0, "event_time"));

    if (!out->event_type || !out->service_id || !out->event_time)
        goto end;

    // paras
    cJSON *paras = cJSON_GetObjectItem(svc0, "paras");
    if (!cJSON_IsObject(paras)) goto end;

    out->url = safe_dup_json_string(
        cJSON_GetObjectItem(paras, "url"));
    out->bucket_name = safe_dup_json_string(
        cJSON_GetObjectItem(paras, "bucket_name"));
    out->object_name = safe_dup_json_string(
        cJSON_GetObjectItem(paras, "object_name"));

    if (!out->url || !out->bucket_name || !out->object_name)
        goto end;

    cJSON *expire = cJSON_GetObjectItem(paras, "expire");
    if (!cJSON_IsNumber(expire)) goto end;
    out->expire = expire->valueint;

    // file_attributes
    cJSON *file_attr = cJSON_GetObjectItem(paras, "file_attributes");
    if (!cJSON_IsObject(file_attr)) goto end;

    out->hash_code = safe_dup_json_string(
        cJSON_GetObjectItem(file_attr, "hash_code"));
    if (!out->hash_code) goto end;

    cJSON *size = cJSON_GetObjectItem(file_attr, "size");
    if (!cJSON_IsNumber(size)) goto end;
    out->size = size->valueint;

    ok = true;

end:
    cJSON_Delete(root);
    if (!ok) free_download_url_info(out);
    return ok;
}