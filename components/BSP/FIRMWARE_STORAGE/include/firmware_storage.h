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
 * @brief 释放固件存储库资源
 */
void firmware_storage_deinit(void);

/**
 * @brief 开始一个新的固件升级会话
 * 
 * @param[in] info 固件信息（版本、大小、校验和等）
 * @param[out] session_id 输出会话ID，用于后续操作
 * @return esp_err_t 
 *         - ESP_OK: 会话创建成功
 *         - ESP_ERR_INVALID_STATE: 已有活跃会话
 *         - ESP_ERR_NO_MEM: 内存不足
 */
esp_err_t firmware_storage_start_upgrade(const firmware_info_t *info, int *session_id);

/**
 * @brief 写入固件数据块
 * 
 * @param[in] session_id 升级会话ID
 * @param[in] data 数据指针
 * @param[in] size 数据大小
 * @param[out] progress 输出当前进度百分比（0-100），可为NULL
 * @return esp_err_t 
 *         - ESP_OK: 写入成功
 *         - ESP_ERR_INVALID_ARG: 参数无效
 *         - ESP_ERR_INVALID_STATE: 会话无效
 */
esp_err_t firmware_storage_write_chunk(int session_id, const uint8_t *data, size_t size, int *progress);

/**
 * @brief 完成固件升级并验证
 * 
 * @param[in] session_id 升级会话ID
 * @return esp_err_t 
 *         - ESP_OK: 升级成功完成
 *         - ESP_ERR_INVALID_STATE: 验证失败
 */
esp_err_t firmware_storage_finalize_upgrade(int session_id);

/**
 * @brief 取消/中止当前固件升级
 * 
 * @param[in] session_id 升级会话ID
 * @return esp_err_t 
 *         - ESP_OK: 取消成功
 */
esp_err_t firmware_storage_cancel_upgrade(int session_id);

/**
 * @brief 检查是否有可用的固件文件
 * 
 * @return true 有可用固件
 * @return false 无可用固件
 */
bool firmware_storage_has_firmware(void);

/**
 * @brief 获取当前固件信息
 * 
 * @param[out] info 输出固件信息
 * @return esp_err_t 
 *         - ESP_OK: 获取成功
 *         - ESP_ERR_NOT_FOUND: 无固件文件
 */
esp_err_t firmware_storage_get_info(firmware_info_t *info);

/**
 * @brief 获取固件文件描述符（用于读取）
 * 
 * @param[out] fd 输出文件描述符
 * @return esp_err_t 
 *         - ESP_OK: 获取成功
 *         - ESP_ERR_NOT_FOUND: 无固件文件
 */
esp_err_t firmware_storage_open_for_read(int *fd);

/**
 * @brief 读取固件数据
 * 
 * @param[in] fd 文件描述符
 * @param[out] buffer 输出缓冲区
 * @param[in] size 要读取的大小
 * @param[out] bytes_read 实际读取的字节数
 * @return esp_err_t 
 *         - ESP_OK: 读取成功
 *         - ESP_ERR_INVALID_ARG: 参数无效
 */
esp_err_t firmware_storage_read_data(int fd, uint8_t *buffer, size_t size, size_t *bytes_read);

/**
 * @brief 关闭固件文件
 * 
 * @param[in] fd 文件描述符
 * @return esp_err_t 
 *         - ESP_OK: 关闭成功
 */
esp_err_t firmware_storage_close(int fd);

/**
 * @brief 删除固件文件
 * 
 * @return esp_err_t 
 *         - ESP_OK: 删除成功
 *         - ESP_ERR_NOT_FOUND: 文件不存在
 */
esp_err_t firmware_storage_delete_firmware(void);

/**
 * @brief 获取存储统计信息
 * 
 * @param[out] stats 输出统计信息
 * @return esp_err_t 
 *         - ESP_OK: 获取成功
 */
esp_err_t firmware_storage_get_stats(firmware_storage_stats_t *stats);

/**
 * @brief 验证固件完整性
 * 
 * @param[in] expected_checksum 预期的校验和，如果为NULL则只检查基本完整性
 * @param[out] actual_checksum 实际校验和输出缓冲区（可选）
 * @return esp_err_t 
 *         - ESP_OK: 验证通过
 *         - ESP_ERR_INVALID_CRC: 校验和不匹配
 *         - ESP_ERR_NOT_FOUND: 文件不存在
 */
esp_err_t firmware_storage_verify_integrity(const uint8_t *expected_checksum, uint8_t *actual_checksum);

/**
 * @brief 获取库版本信息
 * 
 * @return const char* 版本字符串
 */
const char *firmware_storage_get_version(void);

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