#ifndef __FIRMWARE_STORAGE_H__
#define __FIRMWARE_STORAGE_H__

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "firmware_storage_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup firmware_storage_api 固件存储库API
 * @brief 安全存储和管理固件的SPIFFS库
 */

/**
 * @brief 初始化固件存储库
 * 
 * @param[in] config 配置参数，如果为NULL则使用默认配置
 * @return esp_err_t 
 *         - ESP_OK: 初始化成功
 *         - ESP_FAIL: 初始化失败
 */
esp_err_t firmware_storage_init(const firmware_storage_config_t *config);

/**
 * @brief 获取存储统计信息
 * 
 * @param[out] stats 输出统计信息
 * @return esp_err_t 
 *         - ESP_OK: 获取成功
 */
esp_err_t firmware_storage_get_stats(firmware_storage_stats_t *stats);

/**
 * @brief spiffs 状态检查
 * 
 * @param[in] config 配置参数，如果为NULL则使用默认配置
 * @return esp_err_t 
 *         - ESP_OK: 初始化成功
 *         - ESP_FAIL: 初始化失败
 */
void firmware_storage_check(const firmware_storage_config_t *config);

#ifdef __cplusplus
}
#endif

#endif /* __FIRMWARE_STORAGE_H__ */