
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

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

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
