
/*
 * obs_http_single.h
 * 量产级 ESP32 + 华为 OBS HTTP 客户端头文件
 * 模式：MQTT下发临时URL（设备侧不计算签名）
 *
 * 功能：
 *  - 文件下载
 *  - 文件上传
 *  - OBS桶文件列表查询
 */

#ifndef OBS_HTTP_SINGLE_H
#define OBS_HTTP_SINGLE_H

#include "esp_http_client.h" // 确保包含 ESP-IDF HTTP 客户端库
#include "esp_err.h"
#include "json_processor.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LOGIC_URL      "http://111.59.118.25:18083/my/login"
#define LOGIC_NAME     "lixiaoxiang"
#define LOGIC_PASSWORD "Lixiaoxiang001"

#define DOWNLOAD_URL    "http://111.59.118.25:18083/software/getavailableversion"
#define PRODUCT_CODE   "test_code"
#define PLAT_FORM_CODE "mcu"
#define VERSION         "v1.0.0"

// 定义 HTTP 响应体的最大缓冲区大小。
// 请根据您的应用需求调整此值，以避免内存溢出或数据截断。
#define MAX_HTTP_OUTPUT_BUFFER 2048 

/**
 * @brief 通用 HTTP 响应结构体
 */
typedef struct {
    char *buffer;       ///< 存储 HTTP 响应体的缓冲区
    int len;            ///< 实际接收到的响应体长度
    int status_code;    ///< HTTP 响应状态码 (例如 200, 404, 500)
    // 您可以根据需要在此处添加其他字段，如响应头等
} generic_http_response_t;

/**
 * @brief 发送一个通用的 HTTP 请求。
 *
 * @param url 请求的目标 URL。
 * @param method HTTP 请求方法 (例如 HTTP_METHOD_GET, HTTP_METHOD_POST)。
 * @param auth_token 可选的认证令牌 (Bearer token)。如果不需要认证，传入 NULL。
 * @param content_type 可选的 Content-Type 请求头。如果传入 NULL，则对于 POST/PUT 请求默认为 "application/json"。
 * @param post_data 可选的 POST/PUT 请求体数据。对于 GET/DELETE 请求，传入 NULL。
 * @param response_out 用于接收响应的 generic_http_response_t 结构体指针。
 *                     此函数会为 response_out->buffer 分配内存，调用者负责在完成后释放。
 * @return 如果 HTTP 请求成功 (即客户端执行无错误，不代表业务状态码 200)，返回 true；否则返回 false。
 */
bool http_send_request(
    const char *url,
    esp_http_client_method_t method,
    const char *auth_token,
    const char *content_type,
    const char *post_data,
    generic_http_response_t *response_out
);

extern ota_info_t g_download_info;
extern const char ca_cert[];
/**
 * @brief 通过临时URL从OBS下载文件
 *
 * @param url         MQTT下发的临时下载URL
 * @param local_path 本地保存路径（SPIFFS/SD卡路径）
 *
 * @return
 *  - ESP_OK 成功
 *  - ESP_FAIL 失败
 *  - ESP_ERR_INVALID_ARG 参数错误
 */
esp_err_t obs_http_download(const char *url, const char *local_path);
esp_err_t download_to_usb(const char *url, const char *filename);
bool http_login(login_response_t *resp);
bool http_get_version(const char *token);
bool verify_file_md5(const char *path, const char *expected_md5);
/**
 * @brief 通过临时URL上传文件到OBS
 *
 * @param url         MQTT下发的临时上传URL（PUT）
 * @param local_path 本地文件路径
 *
 * @return
 *  - ESP_OK 成功
 *  - ESP_FAIL 失败
 *  - ESP_ERR_INVALID_ARG 参数错误
 */
esp_err_t obs_http_upload(const char *url, const char *local_path);

/**
 * @brief 查询OBS桶文件列表
 *
 * 通常返回XML或JSON格式，
 * 保存到本地文件后自行解析
 *
 * @param url        MQTT下发的桶查询URL
 * @param save_path 本地保存路径
 *
 * @return
 *  - ESP_OK 成功
 *  - ESP_FAIL 失败
 *  - ESP_ERR_INVALID_ARG 参数错误
 */
esp_err_t obs_http_list_bucket(const char *url, const char *save_path);

#ifdef __cplusplus
}
#endif

#endif /* OBS_HTTP_SINGLE_H */
