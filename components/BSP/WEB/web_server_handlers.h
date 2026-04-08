/**
 * @file web_server_handlers.h
 * @brief Web服务器处理器头文件
 *
 * 该头文件定义了ESP32 Web服务器的HTTP请求处理器函数声明、
 * 配置结构体以及相关的宏定义。主要功能包括：
 * - 仪器配置管理（保存/加载到NVS）
 * - WiFi操作（扫描、连接、信息获取）
 * - 数据上传配置管理
 * - 产品/平台/版本信息查询
 * - OTA升级处理
 *
 * @author lixiaoxiang2023
 * @date 2024
 */

#ifndef WEB_SERVER_HANDLERS_H
#define WEB_SERVER_HANDLERS_H

/*==============================================================================
 * 标准库头文件
 *============================================================================*/
#include <dirent.h>
#include <sys/stat.h>

/*==============================================================================
 * ESP-IDF 组件头文件
 *============================================================================*/
#include "esp_err.h"
#include "esp_http_server.h"

/*==============================================================================
 * 第三方库头文件
 *============================================================================*/
#include "cJSON.h"

/*==============================================================================
 * 项目内部头文件
 *============================================================================*/
#include "json_processor.h"
#include "obs_http.h"

/*==============================================================================
 * 宏定义
 *============================================================================*/

/**
 * @brief NVS存储命名空间
 */
#define NVS_NAMESPACE "config"

/**
 * @brief NVS存储键定义
 * @{
 */
#define NVS_KEY_PLATFORM_CODE     "plat_id"      ///< 平台ID（发送给服务器用）
#define NVS_KEY_PLATFORM_CODE_STR "plat_code"    ///< 平台代码（HTTP接收用）
#define NVS_KEY_PLATFORM_NAME     "plat_name"    ///< 平台名称（本地显示用）
#define NVS_KEY_PRODUCT_CODE      "prod_code"    ///< 产品代码
#define NVS_KEY_FIRMWARE_VERSION  "fw_version"   ///< 固件版本
#define NVS_KEY_LOCAL_FILE        "local_file"   ///< 本地文件路径
#define NVS_KEY_UPLOAD_SERVER     "upload_srv"   ///< 上传服务器地址
#define NVS_KEY_WIFI_SSID         "wifi_ssid"    ///< WiFi SSID
#define NVS_KEY_WIFI_PASSWORD     "wifi_pwd"     ///< WiFi密码
/** @} */
/*==============================================================================
 * 类型定义
 *============================================================================*/

/**
 * @brief 仪器配置结构体
 *
 * 用于存储和管理仪器相关的配置信息，包括产品代码、平台信息和固件版本。
 */
typedef struct {
    char product_code[32];     ///< 产品代码字符串
    char platform_code[32];    ///< 平台代码（HTTP接收用）
    char platform_name[64];    ///< 平台名称（如"ESP32-S3-Dev"）
    char platform_id[16];      ///< 平台ID字符串（如"13"）
    char firmware_version[32]; ///< 固件版本字符串
} instrument_config_t;

/**
 * @brief 数据上传配置结构体
 *
 * 用于存储数据上传相关的配置信息。
 */
typedef struct {
    char local_file[128];      ///< 本地文件路径
    char upload_server[128];  ///< 上传服务器地址
} data_upload_config_t;

/**
 * @brief OTA升级消息结构体
 *
 * 用于传递OTA升级相关的信息到升级队列。
 */
typedef struct {
    char product_code[32];     ///< 产品代码
    char platform_code[32];    ///< 平台代码
    char firmware_version[32]; ///< 固件版本
    int64_t platform_id;       ///< 平台ID（数字形式）
} ota_msg_t;

/*==============================================================================
 * 全局变量声明
 *============================================================================*/

extern login_response_t g_strResp;  ///< 全局登录响应结构体
extern QueueHandle_t ota_queue;     ///< OTA升级消息队列句柄

/*==============================================================================
 * 函数声明
 *============================================================================*/

/**
 * @brief NVS配置管理函数
 * @{
 */

/**
 * @brief 初始化Web配置的NVS存储
 *
 * 打开NVS命名空间用于存储Web服务器相关的配置数据。
 *
 * @return esp_err_t 操作结果
 *         - ESP_OK: 初始化成功
 *         - ESP_ERR_NVS_NOT_INITIALIZED: NVS未初始化
 *         - ESP_ERR_NVS_NOT_FOUND: 命名空间不存在
 */
esp_err_t init_web_config_nvs(void);

/**
 * @brief 保存仪器配置到NVS存储
 *
 * 将仪器配置结构体中的数据保存到NVS中。
 *
 * @param config 指向仪器配置结构体的指针
 * @return esp_err_t 操作结果
 */
esp_err_t save_instrument_config_to_nvs(const instrument_config_t *config);

/**
 * @brief 从NVS加载仪器配置
 *
 * 从NVS存储中读取仪器配置数据。
 *
 * @param config 指向仪器配置结构体的指针，用于存储读取的数据
 * @return esp_err_t 操作结果
 */
esp_err_t load_instrument_config_from_nvs(instrument_config_t *config);

/**
 * @brief 从NVS加载数据上传配置
 *
 * 从NVS中读取数据上传相关的配置。
 *
 * @param config 指向数据上传配置结构体的指针
 * @return esp_err_t 操作结果
 */
esp_err_t load_data_upload_config_from_nvs(data_upload_config_t *config);

/**
 * @brief 保存数据上传配置到NVS
 *
 * 将数据上传配置保存到NVS存储中。
 *
 * @param config 指向数据上传配置结构体的指针
 * @return esp_err_t 操作结果
 */
esp_err_t save_data_upload_config_to_nvs(const data_upload_config_t *config);

/** @} */

/**
 * @brief HTTP请求处理器函数
 * @{
 */

/**
 * @brief 获取仪器配置处理器
 *
 * 处理前端GET /get_instrument_config请求。
 *
 * @param req HTTP请求结构体指针
 * @return esp_err_t 处理结果
 */
esp_err_t get_instrument_config_handler(httpd_req_t *req);

/**
 * @brief 保存仪器配置处理器
 *
 * 处理前端POST /save_instrument_config请求。
 *
 * @param req HTTP请求结构体指针
 * @return esp_err_t 处理结果
 */
esp_err_t save_instrument_config_handler(httpd_req_t *req);

/**
 * @brief WiFi扫描处理器
 *
 * 处理前端WiFi扫描请求，返回可用WiFi网络列表。
 *
 * @param req HTTP请求结构体指针
 * @return esp_err_t 处理结果
 */
esp_err_t wifi_scan_handler(httpd_req_t *req);

/**
 * @brief WiFi连接处理器
 *
 * 处理前端WiFi连接请求。
 *
 * @param req HTTP请求结构体指针
 * @return esp_err_t 处理结果
 */
esp_err_t connect_wifi_handler(httpd_req_t *req);

/**
 * @brief 获取WiFi信息处理器
 *
 * 处理前端获取当前WiFi连接信息的请求。
 *
 * @param req HTTP请求结构体指针
 * @return esp_err_t 处理结果
 */
esp_err_t get_wifi_info_handler(httpd_req_t *req);

/**
 * @brief USB文件列表处理器
 *
 * 处理前端获取USB设备文件列表的请求。
 *
 * @param req HTTP请求结构体指针
 * @return esp_err_t 处理结果
 */
esp_err_t usb_files_handler(httpd_req_t *req);

/**
 * @brief 获取数据上传配置处理器
 *
 * 处理前端获取数据上传配置的请求。
 *
 * @param req HTTP请求结构体指针
 * @return esp_err_t 处理结果
 */
esp_err_t get_config_handler(httpd_req_t *req);

/**
 * @brief 保存数据上传配置处理器
 *
 * 处理前端保存数据上传配置的请求。
 *
 * @param req HTTP请求结构体指针
 * @return esp_err_t 处理结果
 */
esp_err_t save_config_handler(httpd_req_t *req);

/**
 * @brief 获取产品列表处理器
 *
 * 处理前端获取产品列表的请求。
 *
 * @param req HTTP请求结构体指针
 * @return esp_err_t 处理结果
 */
esp_err_t get_product_list_handler(httpd_req_t *req);

/**
 * @brief 获取平台列表处理器
 *
 * 处理前端GET /api/get_platforms请求。
 *
 * @param req HTTP请求结构体指针
 * @return esp_err_t 处理结果
 */
esp_err_t get_platforms_handler(httpd_req_t *req);

/**
 * @brief 获取版本列表处理器
 *
 * 处理前端GET /api/get_versions请求。
 *
 * @param req HTTP请求结构体指针
 * @return esp_err_t 处理结果
 */
esp_err_t get_versions_handler(httpd_req_t *req);

/**
 * @brief OTA升级处理器
 *
 * 处理前端OTA升级请求。
 *
 * @param req HTTP请求结构体指针
 * @return esp_err_t 处理结果
 */
esp_err_t update_handler(httpd_req_t *req);

/** @} */

/**
 * @brief 外部函数声明
 * @{
 */

/**
 * @brief 应用WiFi配置
 *
 * 连接到指定的WiFi网络。
 *
 * @param ssid WiFi网络名称
 * @param password WiFi密码
 * @return esp_err_t 操作结果
 */
extern esp_err_t wifi_apply_config(const char *ssid, const char *password);

/** @} */

#endif /* WEB_SERVER_HANDLERS_H */