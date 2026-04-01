// json_processor.c
#include "json_processor.h"
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
static const char *TAG = "OTA_PARSER";


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

void free_login_response(login_response_t *out)
{
    if (!out) return;

    free(out->msg);
    free(out->username);
    free(out->real_name);
    free(out->avatar);
    free(out->token);
    free(out->language);
    free(out->email);

    memset(out, 0, sizeof(login_response_t));
}

bool parse_login_response(const char *json_str, login_response_t *out)
{
    if (!json_str || !out) return false;
    memset(out, 0, sizeof(login_response_t));

    cJSON *root = cJSON_Parse(json_str);
    if (!root) return false;

    bool ok = false;

    // code
    cJSON *code = cJSON_GetObjectItem(root, "code");
    if (!cJSON_IsNumber(code)) goto end;
    out->code = code->valueint;

    // msg
    out->msg = safe_dup_json_string(
        cJSON_GetObjectItem(root, "msg"));

    if (!out->msg) goto end;

    // 如果失败，直接返回（只关心code）
    if (out->code != 0) {
        ok = true;
        goto end;
    }

    // data
    cJSON *data = cJSON_GetObjectItem(root, "data");
    if (!cJSON_IsObject(data)) goto end;

    // id
    cJSON *id = cJSON_GetObjectItem(data, "id");
    if (!cJSON_IsNumber(id)) goto end;
    out->id = id->valueint;

    // username
    out->username = safe_dup_json_string(
        cJSON_GetObjectItem(data, "username"));

    // real_name
    out->real_name = safe_dup_json_string(
        cJSON_GetObjectItem(data, "real_name"));

    // avatar
    out->avatar = safe_dup_json_string(
        cJSON_GetObjectItem(data, "avatar"));

    // status
    cJSON *status = cJSON_GetObjectItem(data, "status");
    if (!cJSON_IsNumber(status)) goto end;
    out->status = status->valueint;

    // token
    out->token = safe_dup_json_string(
        cJSON_GetObjectItem(data, "token"));

    // language
    out->language = safe_dup_json_string(
        cJSON_GetObjectItem(data, "language"));

    // weak_passwd
    cJSON *weak = cJSON_GetObjectItem(data, "weak_passwd");
    if (!cJSON_IsBool(weak)) goto end;
    out->weak_passwd = cJSON_IsTrue(weak);

    // email
    out->email = safe_dup_json_string(
        cJSON_GetObjectItem(data, "email"));

    // 关键字段检查（你可以按需删减）
    if (!out->username || !out->token)
        goto end;

    ok = true;

end:
    cJSON_Delete(root);
    if (!ok) free_login_response(out);
    return ok;
}

bool parse_ota_response(const char *json_data, ota_info_t *out_info) {
    if (json_data == NULL || out_info == NULL) return false;

    cJSON *root = cJSON_Parse(json_data);
    if (root == NULL) {
        ESP_LOGE(TAG, "JSON 语法错误");
        return false;
    }

    // 解析顶层 code
    cJSON *code = cJSON_GetObjectItem(root, "code");
    if (cJSON_IsNumber(code)) out_info->code = code->valueint;

    // 进入 data 层
    cJSON *data = cJSON_GetObjectItem(root, "data");
    if (cJSON_IsObject(data)) {
        cJSON *name = cJSON_GetObjectItem(data, "name");
        cJSON *ver = cJSON_GetObjectItem(data, "version");
        cJSON *time = cJSON_GetObjectItem(data, "createdat");

        if (cJSON_IsString(name)) strlcpy(out_info->name, name->valuestring, sizeof(out_info->name));
        if (cJSON_IsString(ver)) strlcpy(out_info->version, ver->valuestring, sizeof(out_info->version));
        if (cJSON_IsString(time)) strlcpy(out_info->created_at, time->valuestring, sizeof(out_info->created_at));

        // 进入 files 数组
        cJSON *files = cJSON_GetObjectItem(data, "files");
        if (cJSON_IsArray(files) && cJSON_GetArraySize(files) > 0) {
            cJSON *f_obj = cJSON_GetArrayItem(files, 0); // 取第一个文件
            
            cJSON *f_name = cJSON_GetObjectItem(f_obj, "name");
            cJSON *f_url = cJSON_GetObjectItem(f_obj, "url");
            cJSON *f_md5 = cJSON_GetObjectItem(f_obj, "md5");
            cJSON *f_size = cJSON_GetObjectItem(f_obj, "size");

            if (cJSON_IsString(f_name)) strlcpy(out_info->file_name, f_name->valuestring, sizeof(out_info->file_name));
            if (cJSON_IsString(f_url)) strlcpy(out_info->url, f_url->valuestring, sizeof(out_info->url));
            if (cJSON_IsString(f_md5)) strlcpy(out_info->md5, f_md5->valuestring, sizeof(out_info->md5));
            if (cJSON_IsNumber(f_size)) out_info->size = (uint32_t)f_size->valueint;
        }
    }

    cJSON_Delete(root);
    return true;
}

// 假设我们最多支持 10 个产品显示在下拉框
product_info_t g_products[30];
int g_product_count = 0;

void parse_all_products(const char *json_str) {
    cJSON *root = cJSON_Parse(json_str);
    if (root == NULL) return;

    cJSON *data_array = cJSON_GetObjectItem(root, "data");
    if (cJSON_IsArray(data_array)) {
        g_product_count = cJSON_GetArraySize(data_array);
        
        // 限制最大解析数量，防止内存溢出
        int limit = (g_product_count < 30) ? g_product_count : 30;
        
        for (int i = 0; i < limit; i++) {
            cJSON *item = cJSON_GetArrayItem(data_array, i);
            if (item) {
                cJSON *name = cJSON_GetObjectItem(item, "name");
                cJSON *code = cJSON_GetObjectItem(item, "code");
                
                if (cJSON_IsString(name) && cJSON_IsString(code)) {
                    strncpy(g_products[i].name, name->valuestring, sizeof(g_products[i].name) - 1);
                    strncpy(g_products[i].code, code->valuestring, sizeof(g_products[i].code) - 1);
                    
                    ESP_LOGI("PARSE", "找到产品[%d]: %s (%s)", i, g_products[i].name, g_products[i].code);
                }
            }
        }
    }
    cJSON_Delete(root);
}