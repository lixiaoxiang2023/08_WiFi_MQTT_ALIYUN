#ifndef WEB_SERVER_HANDLERS_H
#define WEB_SERVER_HANDLERS_H

#include "esp_err.h"
#include "esp_http_server.h"
#include "cJSON.h"
#include <dirent.h>     // 添加这些，因为文件操作转移到这里
#include <sys/stat.h>   // 添加这些
#include "json_processor.h"  // 添加这些
#include "obs_http.h" // 

// #include "esp_wifi_types.h" // 移除这一行
// NVS 存储的键定义
#define NVS_NAMESPACE "config"
#define NVS_KEY_PLATFORM_CODE "plat_id"
#define NVS_KEY_PRODUCT_CODE "prod_code"
#define NVS_KEY_FIRMWARE_VERSION "fw_version"
#define NVS_KEY_LOCAL_FILE "local_file"
#define NVS_KEY_UPLOAD_SERVER "upload_srv"
#define NVS_KEY_WIFI_SSID "wifi_ssid"
#define NVS_KEY_WIFI_PASSWORD "wifi_pwd"
#define NVS_KEY_PLATFORM_NAME    "plat_name" // 存储 Name (如 "ESP32-S3")
/**
 * @brief 结构体，用于在 C 代码中统一管理仪器配置。
 */
typedef struct {
    char product_code[32];
    char platform_name[64];  // 存放 "ESP32-S3-Dev"
    char platform_id[16];    // 存放 "13"
    char firmware_version[32];
} instrument_config_t;

/**
 * @brief 结构体，用于统一管理数据上传相关配置。
 */
typedef struct {
    char local_file[128];       // 本地文件名称
    char upload_server[128];    // 上传服务器地址或路径
} data_upload_config_t;
typedef struct {
    char product_code[32];
    char platform_code[32];
    char firmware_version[32];  
    int64_t platform_id;

} ota_msg_t;

/**
 * @brief 初始化 NVS 存储。
 * @return ESP_OK 成功，否则失败。
 */
esp_err_t init_web_config_nvs(void);
extern login_response_t g_strResp;
extern QueueHandle_t ota_queue;
/**
 * @brief 将仪器配置保存到 NVS。
 * @param config 要保存的配置结构体指针。
 * @return ESP_OK 成功，否则失败。
 */
esp_err_t save_instrument_config_to_nvs(const instrument_config_t *config);

/**
 * @brief 从 NVS 加载仪器配置。
 * @param config 用于存储加载配置的结构体指针。
 * @return ESP_OK 成功，否则失败。
 */
esp_err_t load_instrument_config_from_nvs(instrument_config_t *config);

/**
 * @brief 从 NVS 加载数据上传配置。
 * @param config 用于存储加载配置的结构体指针。
 * @return ESP_OK 成功，否则失败。
 */
esp_err_t load_data_upload_config_from_nvs(data_upload_config_t *config);

/**
 * @brief 将数据上传配置保存到 NVS。
 * @param config 要保存的配置结构体指针。
 * @return ESP_OK 成功，否则失败。
 */
esp_err_t save_data_upload_config_to_nvs(const data_upload_config_t *config);


// --- HTTP Request Handlers --- (从 wifi_config.c 移动过来，以及新的仪器配置处理器)
esp_err_t get_instrument_config_handler(httpd_req_t *req);
esp_err_t save_instrument_config_handler(httpd_req_t *req);

esp_err_t wifi_scan_handler(httpd_req_t *req);
extern esp_err_t wifi_apply_config(const char *ssid, const char *password); // 声明 extern 确保可调用
esp_err_t connect_wifi_handler(httpd_req_t *req);
esp_err_t usb_files_handler(httpd_req_t *req);
esp_err_t get_config_handler(httpd_req_t *req); // 用于获取数据上传配置
esp_err_t get_wifi_info_handler(httpd_req_t *req);
esp_err_t save_config_handler(httpd_req_t *req); // 用于保存数据上传配置
esp_err_t get_product_list_handler(httpd_req_t *req);
esp_err_t get_versions_handler(httpd_req_t *req);
esp_err_t get_platforms_handler(httpd_req_t *req);
esp_err_t update_handler(httpd_req_t *req);
#endif // WEB_SERVER_HANDLERS_H