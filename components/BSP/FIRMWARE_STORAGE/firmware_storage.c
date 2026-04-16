#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/unistd.h>
#include <dirent.h>
#include <sys/time.h>
#include <time.h>
#include <errno.h>  // 添加头文件
#include "esp_log.h"
#include "esp_spiffs.h"
#include "mbedtls/md.h"
#include "esp_rom_crc.h"

#include "firmware_storage.h"
#include "firmware_storage_priv.h"

static const char *TAG = "FirmwareStorage";
static firmware_storage_ctx_t s_ctx = {0};
static const char *LIB_VERSION = "1.0.0";

// 默认配置
static const firmware_storage_config_t s_default_config = FIRMWARE_STORAGE_DEFAULT_CONFIG();

// 公共API实现
esp_err_t firmware_storage_init(const firmware_storage_config_t *config) {
    if (s_ctx.initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Initializing Firmware Storage Library v%s", LIB_VERSION);
    
    // 使用配置或默认值
    if (config) {
        memcpy(&s_ctx.config, config, sizeof(firmware_storage_config_t));
    } else {
        memcpy(&s_ctx.config, &s_default_config, sizeof(firmware_storage_config_t));
    }
    
    // 初始化SPIFFS
    esp_vfs_spiffs_conf_t spiffs_conf = {
        .base_path = s_ctx.config.base_path,
        .partition_label = s_ctx.config.partition_label,
        .format_if_mount_failed = s_ctx.config.format_if_mount_failed,
        .max_files = s_ctx.config.max_files,
       // .grow_on_mount = false,
    };
    
    esp_err_t ret = esp_vfs_spiffs_register(&spiffs_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPIFFS: %s", esp_err_to_name(ret));
        return ret;
    }
    // 初始化文件路径
    snprintf(s_ctx.firmware_path, sizeof(s_ctx.firmware_path), 
             "%s/firmware.bin", s_ctx.config.base_path);
    snprintf(s_ctx.firmware_temp_path, sizeof(s_ctx.firmware_temp_path),
             "%s/firmware%s", s_ctx.config.base_path, s_ctx.config.temp_suffix);
    
    // 初始化统计信息
    memset(&s_ctx.stats, 0, sizeof(s_ctx.stats));
    s_ctx.active_session = NULL;
    s_ctx.initialized = true;
    
    // 获取存储空间信息
    esp_spiffs_info(s_ctx.config.partition_label, 
                    &s_ctx.stats.total_space, 
                    &s_ctx.stats.used_space);
    s_ctx.stats.free_space = s_ctx.stats.total_space - s_ctx.stats.used_space;
    
    ESP_LOGI(TAG, "Storage initialized: %s (total: %d KB, free: %d KB)", 
            s_ctx.config.base_path, 
            s_ctx.stats.total_space / 1024,
            s_ctx.stats.free_space / 1024);
    
    return ESP_OK;
}

esp_err_t firmware_storage_get_stats(firmware_storage_stats_t *stats) {
    if (!s_ctx.initialized || !stats) return ESP_FAIL;
    
    memcpy(stats, &s_ctx.stats, sizeof(firmware_storage_stats_t));
    
    // 计算文件数量
    DIR *dir = opendir(s_ctx.config.base_path);
    if (dir) {
        struct dirent *entry;
        uint32_t count = 0;
        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_type == DT_REG) {
                count++;
            }
        }
        closedir(dir);
        stats->file_count = count;
    }
    
    return ESP_OK;
}

void firmware_storage_check(const firmware_storage_config_t *config)
{
        // 1. 初始化库（使用默认配置）
    esp_err_t ret = firmware_storage_init(config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize firmware storage");
        return;
    }
    //spiffs_format_and_remount();
    // 2. 获取存储统计
    firmware_storage_stats_t stats;
    if (firmware_storage_get_stats(&stats) == ESP_OK) {
        ESP_LOGI(TAG, "Storage: %d/%d KB free", 
                stats.free_space/1024, stats.total_space/1024);
    }
}
