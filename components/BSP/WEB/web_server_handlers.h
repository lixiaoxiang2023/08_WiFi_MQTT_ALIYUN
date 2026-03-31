#ifndef WEB_SERVER_HANDLERS_H
#define WEB_SERVER_HANDLERS_H

#include "esp_err.h"
#include "esp_http_server.h"
#include "cJSON.h"

// NVS 存储的键定义
#define NVS_NAMESPACE "config"
#define NVS_KEY_PLATFORM_CODE "plat_code"
#define NVS_KEY_PRODUCT_CODE "prod_code"
#define NVS_KEY_FIRMWARE_VERSION "fw_version"
#define NVS_KEY_LOCAL_FILE "local_file"
#define NVS_KEY_UPLOAD_SERVER "upload_srv"
#define NVS_KEY_WIFI_SSID "wifi_ssid"
#define NVS_KEY_WIFI_PASSWORD "wifi_pwd"

/**
 * @brief 结构体，用于在 C 代码中统一管理仪器配置。
 */
typedef struct {
    char platform_code[32];
    char product_code[32];
    char firmware_version[32];
    char local_file[128];
    char upload_server[128];
} instrument_config_t;

/**
 * @brief 初始化 NVS 存储。
 * @return ESP_OK 成功，否则失败。
 */
esp_err_t init_web_config_nvs(void);

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

// --- HTTP Request Handlers ---
esp_err_t get_instrument_config_handler(httpd_req_t *req);
esp_err_t save_instrument_config_handler(httpd_req_t *req);
// 你需要在这里添加其他已有的 Web 服务器处理器声明，例如：
// esp_err_t http_server_page_handler(httpd_req_t *req);
// esp_err_t wifi_scan_handler(httpd_req_t *req);
// esp_err_t wifi_connect_handler(httpd_req_t *req);
// esp_err_t get_wifi_info_handler(httpd_req_t *req);
// esp_err_t usb_files_handler(httpd_req_t *req);

#endif // WEB_SERVER_HANDLERS_H