// json_processor.h
#ifndef JSON_PROCESSOR_H
#define JSON_PROCESSOR_H

#include "cJSON.h"
#include "esp_err.h"
#include <stdbool.h>
typedef struct {
    char *key;
    char *value_str;
    double value_num;
    bool value_bool;
    cJSON *value_obj;
    cJSON *value_arr;
    bool is_string;
    bool is_number;
    bool is_bool;
    bool is_object;
    bool is_array;
    bool is_null;
} kv_pair_t;

// 通用JSON处理器
typedef struct {
    char *topic;
    char *device_id;
    uint32_t timestamp;
    kv_pair_t *pairs;
    size_t pair_count;
} json_packet_t;

typedef struct {
    char *object_device_id;

    char *event_type;
    char *service_id;
    char *event_time;

    char *url;
    char *bucket_name;
    char *object_name;
    int   expire;

    char *hash_code;
    int   size;
} download_url_info_t;
typedef struct {
    int code;              // 0成功 1失败
    char *msg;

    // data
    int id;
    char *username;
    char *real_name;
    char *avatar;
    int status;
    char *token;
    char *language;
    int weak_passwd;       // bool用int
    char *email;

} login_response_t;
typedef struct {
    char file_name[64];       // 对应 files[0].name
    char url[1024];           // 对应 files[0].url，S3 签名 URL 建议给到 1024
    char md5[33];             // 对应 files[0].md5
    uint32_t size;            // 对应 files[0].size
} ota_file_t;
typedef struct {
    // 固件基础信息
    char name[64];           // 对应 data.name
    char version[32];        // 对应 data.version
    char created_at[32];     // 对应 data.createdat
    
    // 文件详细信息 (files[0])
    char file_name[64];      // 对应 files[0].name
    char url[1024];          // 对应 files[0].url (重要：必须足够长)
    char md5[33];            // 对应 files[0].md5
    uint32_t size;           // 对应 files[0].size
    
    // 业务状态
    int code;                // 顶层 code
} ota_info_t;
typedef struct {
    int code;                 // 顶层状态码
    char name[64];            // data.name (V1.0.1测试版)
    char version[32];         // data.version (v1.0.1)
    char created_at[32];      // data.createdat
    ota_file_t file;          // 暂取数组第一个文件
    char msg[64];             // 顶层消息
} ota_response_t;
/**
 * @brief 解析下载URL响应JSON
 * @param json_str 输入JSON字符串（以\0结尾）
 * @param out 输出结构体，内部字符串会动态分配，需要调用 free_download_url_info() 释放
 * @return true 成功，false 失败
 */
bool parse_download_url_response(const char *json_str, download_url_info_t *out);

/** 释放 parse_download_url_response() 分配的内存 */
void free_download_url_info(download_url_info_t *info);
esp_err_t create_json_packet(json_packet_t *packet, cJSON **output);
esp_err_t parse_json_packet(const char *json_str, json_packet_t *packet);
void free_json_packet(json_packet_t *packet);
bool parse_login_response(const char *json_str, login_response_t *out);
void free_login_response(login_response_t *out);
bool parse_ota_response(const char *json_data, ota_info_t *out_info);
#endif
