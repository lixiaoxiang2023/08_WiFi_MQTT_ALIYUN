#ifndef __FIRMWARE_STORAGE_PRIV_H__
#define __FIRMWARE_STORAGE_PRIV_H__

#include "firmware_storage_types.h"
#include <stdio.h>

// 私有结构体，不暴露给用户
typedef struct {
    int id;
    firmware_info_t info;
    FILE *temp_file;
    size_t written_bytes;
    char temp_path[128];
    char final_path[128];
    upgrade_state_t state;
    uint32_t start_time;
} upgrade_session_t;

// 库内部全局上下文
typedef struct {
    firmware_storage_config_t config;
    upgrade_session_t *active_session;
    firmware_storage_stats_t stats;
    bool initialized;
    char firmware_path[128];
    char firmware_temp_path[128];
} firmware_storage_ctx_t;

// 内部函数声明
esp_err_t _create_temp_filename(char *buffer, size_t size, const char *version);
esp_err_t _validate_write_conditions(size_t required_size);
esp_err_t _compute_file_checksum(const char *path, uint8_t *checksum, checksum_type_t type);
esp_err_t _atomic_rename(const char *old_path, const char *new_path);
void _update_stats(bool success);
bool _is_valid_firmware_file(const char *path);

#endif /* __FIRMWARE_STORAGE_PRIV_H__ */