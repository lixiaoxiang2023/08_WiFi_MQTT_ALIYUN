#ifndef __FIRMWARE_STORAGE_TYPES_H__
#define __FIRMWARE_STORAGE_TYPES_H__

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// 校验和类型
typedef enum {
    CHECKSUM_TYPE_NONE = 0,
    CHECKSUM_TYPE_CRC32,
    CHECKSUM_TYPE_MD5,
    CHECKSUM_TYPE_SHA256,
    CHECKSUM_TYPE_SHA1
} checksum_type_t;

// 固件信息结构
typedef struct {
    char version[32];           // 版本号，如 "1.2.3"
    char device_type[32];       // 设备类型，如 "STM32F407"
    size_t total_size;          // 固件总大小
    uint32_t crc32;             // CRC32校验和
    uint8_t sha256[32];         // SHA256校验和
    uint32_t timestamp;         // 时间戳
    uint16_t hw_version;        // 硬件版本
    uint16_t fw_version_major;  // 主版本号
    uint16_t fw_version_minor;  // 次版本号
    uint16_t fw_version_patch;  // 修订号
    uint8_t reserved[32];       // 保留字段
} firmware_info_t;

// 存储配置结构
typedef struct {
    const char *base_path;      // SPIFFS挂载路径
    const char *partition_label;// 分区标签
    size_t max_file_size;       // 最大文件大小（字节）
    uint8_t max_files;          // 最大文件数
    bool format_if_mount_failed;// 挂载失败时是否格式化
    bool enable_cache;          // 是否启用缓存
    checksum_type_t checksum_type; // 校验和类型
    const char *temp_suffix;    // 临时文件后缀，如 ".tmp"
} firmware_storage_config_t;

// 默认配置
#define FIRMWARE_STORAGE_DEFAULT_CONFIG() { \
    .base_path = "/spiffs", \
    .partition_label = "storage", \
    .max_file_size = 4 * 1024 * 1024, /* 4MB */ \
    .max_files = 3, \
    .format_if_mount_failed = true, \
    .enable_cache = true, \
    .checksum_type = CHECKSUM_TYPE_SHA256, \
    .temp_suffix = ".bin" \
}

// 存储统计信息
typedef struct {
    size_t total_space;         // 总空间（字节）
    size_t used_space;          // 已用空间（字节）
    size_t free_space;          // 可用空间（字节）
    uint32_t file_count;        // 文件数量
    uint32_t upgrade_count;     // 升级次数
    uint32_t failed_count;      // 失败次数
    uint32_t last_upgrade_time; // 最后升级时间
} firmware_storage_stats_t;

// 升级会话状态
typedef enum {
    UPGRADE_STATE_IDLE = 0,
    UPGRADE_STATE_WRITING,
    UPGRADE_STATE_VERIFYING,
    UPGRADE_STATE_COMPLETE,
    UPGRADE_STATE_ERROR
} upgrade_state_t;

// 错误码扩展
#define ESP_ERR_FIRMWARE_STORAGE_BASE      0x20000
#define ESP_ERR_FIRMWARE_INVALID_SESSION   (ESP_ERR_FIRMWARE_STORAGE_BASE + 1)
#define ESP_ERR_FIRMWARE_WRITE_FAILED      (ESP_ERR_FIRMWARE_STORAGE_BASE + 2)
#define ESP_ERR_FIRMWARE_VERIFY_FAILED     (ESP_ERR_FIRMWARE_STORAGE_BASE + 3)
#define ESP_ERR_FIRMWARE_STORAGE_FULL      (ESP_ERR_FIRMWARE_STORAGE_BASE + 4)
#define ESP_ERR_FIRMWARE_INVALID_SIZE      (ESP_ERR_FIRMWARE_STORAGE_BASE + 5)

#ifdef __cplusplus
}
#endif

#endif /* __FIRMWARE_STORAGE_TYPES_H__ */