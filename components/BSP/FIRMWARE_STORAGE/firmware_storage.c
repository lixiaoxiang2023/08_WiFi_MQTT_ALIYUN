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

static esp_err_t _compute_sha256(const char *path, uint8_t *output) {
    FILE *file = fopen(path, "rb");
    if (!file) return ESP_FAIL;
    
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 0);
    mbedtls_md_starts(&ctx);
    
    uint8_t buffer[1024];
    size_t bytes_read;
    
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        mbedtls_md_update(&ctx, buffer, bytes_read);
    }
    
    mbedtls_md_finish(&ctx, output);
    mbedtls_md_free(&ctx);
    fclose(file);
    
    return ESP_OK;
}

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

static inline uint64_t get_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
}

void firmware_storage_deinit(void) {
    if (!s_ctx.initialized) return;
    
    // 如果有活跃会话，取消它
    if (s_ctx.active_session) {
        firmware_storage_cancel_upgrade(s_ctx.active_session->id);
    }
    
    // 卸载SPIFFS
    esp_vfs_spiffs_unregister(s_ctx.config.partition_label);
    
    memset(&s_ctx, 0, sizeof(s_ctx));
    ESP_LOGI(TAG, "Firmware storage deinitialized");
}

esp_err_t 
firmware_storage_start_upgrade(const firmware_info_t *info, int *session_id) {
    if (!s_ctx.initialized) return ESP_FAIL;
    if (!info || !session_id) return ESP_ERR_INVALID_ARG;
    if (s_ctx.active_session) return ESP_ERR_INVALID_STATE;
    
    // 验证固件大小
    if (info->total_size == 0 || info->total_size > s_ctx.config.max_file_size) {
        ESP_LOGE(TAG, "Invalid firmware size: %d", info->total_size);
        return ESP_ERR_FIRMWARE_INVALID_SIZE;
    }
    
    // 检查可用空间（需要2倍空间用于安全操作）
    if (info->total_size * 2 > s_ctx.stats.free_space) {
        ESP_LOGE(TAG, "Insufficient storage space: need %d, free %d", 
                info->total_size * 2, s_ctx.stats.free_space);
        return ESP_ERR_FIRMWARE_STORAGE_FULL;
    }
    
    // 创建升级会话
    upgrade_session_t *session = calloc(1, sizeof(upgrade_session_t));
    if (!session) return ESP_ERR_NO_MEM;
    
    static int next_session_id = 1;
    session->id = next_session_id++;
    memcpy(&session->info, info, sizeof(firmware_info_t));
    session->state = UPGRADE_STATE_WRITING;
    //session->start_time = esp_timer_get_time() / 1000; // 毫秒
    session->start_time = get_time_ms();
    // 生成文件路径
    snprintf(session->temp_path, sizeof(session->temp_path),
             "%s/fw_%s_%d%s", s_ctx.config.base_path, 
             info->version, session->id, s_ctx.config.temp_suffix);
    snprintf(session->final_path, sizeof(session->final_path),
             "%s/firmware.bin", s_ctx.config.base_path);
    
    // 打开临时文件
    session->temp_file = fopen(session->temp_path, "wb");
    if (!session->temp_file) {
        ESP_LOGE(TAG, "Failed to open temp file: %s", session->temp_path);
        free(session);
        return ESP_ERR_FIRMWARE_WRITE_FAILED;
    }
    
    s_ctx.active_session = session;
    *session_id = session->id;
    
    ESP_LOGI(TAG, "Upgrade session %d started: %s (%d bytes)", 
            session->id, info->version, info->total_size);
    
    return ESP_OK;
}

esp_err_t firmware_storage_write_chunk(int session_id, const uint8_t *data, size_t size, int *progress) {
    if (!s_ctx.initialized) return ESP_FAIL;
    if (!data || size == 0) return ESP_ERR_INVALID_ARG;
    
    upgrade_session_t *session = s_ctx.active_session;
    if (!session || session->id != session_id || session->state != UPGRADE_STATE_WRITING) {
        return ESP_ERR_FIRMWARE_INVALID_SESSION;
    }
    
    // 检查是否超出预期大小
    if (session->written_bytes + size > session->info.total_size) {
        ESP_LOGE(TAG, "Write would exceed expected size: %d + %d > %d",
                session->written_bytes, size, session->info.total_size);
        return ESP_ERR_FIRMWARE_INVALID_SIZE;
    }
    
    // 写入数据
    size_t written = fwrite(data, 1, size, session->temp_file);
    if (written != size) {
        ESP_LOGE(TAG, "Write failed: %d of %d bytes", written, size);
        return ESP_ERR_FIRMWARE_WRITE_FAILED;
    }
    
    session->written_bytes += written;
    
    // 计算进度
    if (progress) {
        *progress = (session->written_bytes * 100) / session->info.total_size;
    }
    
    // 定期同步（每写入64KB或10%进度）
    static size_t last_sync = 0;
    if (session->written_bytes - last_sync >= 64 * 1024 || 
        (progress && *progress % 10 == 0 && *progress != 0)) {
        fflush(session->temp_file);
        last_sync = session->written_bytes;
        ESP_LOGD(TAG, "Progress: %d%% (%d/%d bytes)", 
                progress ? *progress : 0, 
                session->written_bytes, session->info.total_size);
    }
    
    return ESP_OK;
}

esp_err_t firmware_storage_finalize_upgrade(int session_id) {
    if (!s_ctx.initialized) return ESP_FAIL;
    
    upgrade_session_t *session = s_ctx.active_session;
    if (!session || session->id != session_id || session->state != UPGRADE_STATE_WRITING) {
        return ESP_ERR_FIRMWARE_INVALID_SESSION;
    }
    
    ESP_LOGI(TAG, "Finalizing upgrade session %d", session_id);
    session->state = UPGRADE_STATE_VERIFYING;
    
    // 确保所有数据写入磁盘
    fflush(session->temp_file);
    fsync(fileno(session->temp_file));
    fclose(session->temp_file);
    session->temp_file = NULL;
    
    // 验证文件大小
    struct stat st;
    if (stat(session->temp_path, &st) != 0) {
        ESP_LOGE(TAG, "Failed to stat temp file");
        session->state = UPGRADE_STATE_ERROR;
        goto cleanup;
    }
    
    if (st.st_size != session->info.total_size) {
        ESP_LOGE(TAG, "File size mismatch: expected %d, got %d",
                session->info.total_size, (int)st.st_size);
        session->state = UPGRADE_STATE_ERROR;
        goto cleanup;
    }
 
    // 删除旧固件（如果存在）
    struct stat state;

    if(stat(session->final_path,&state) == 0)
    {
        if (unlink(session->final_path) != 0) {
            ESP_LOGW(TAG, "Failed to delete old firmware, continuing...");
        }
    }

    // 原子重命名
    if (rename(session->temp_path, session->final_path) != 0) {
        ESP_LOGE(TAG, "Atomic rename failed: %s (errno=%d)", strerror(errno), errno);
        ESP_LOGE(TAG, "Atomic rename failed");
        session->state = UPGRADE_STATE_ERROR;
        goto cleanup;
    }
    
    // 最终同步
    //sync();
    
    // 更新统计信息
    session->state = UPGRADE_STATE_COMPLETE;
    s_ctx.stats.upgrade_count++;
    s_ctx.stats.last_upgrade_time = time(NULL);
    
    ESP_LOGI(TAG, "Upgrade session %d completed successfully: %s",
            session_id, session->info.version);
    
cleanup:
    // 清理临时文件（如果存在且失败）
    if (session->state == UPGRADE_STATE_ERROR) {
        unlink(session->temp_path);
        s_ctx.stats.failed_count++;
    }
    
    // 清理会话
    if (session->temp_file) {
        fclose(session->temp_file);
    }
    free(session);
    s_ctx.active_session = NULL;
    
    // 更新存储统计
    esp_spiffs_info(s_ctx.config.partition_label, 
                    &s_ctx.stats.total_space, 
                    &s_ctx.stats.used_space);
    s_ctx.stats.free_space = s_ctx.stats.total_space - s_ctx.stats.used_space;
    
    return (session->state == UPGRADE_STATE_COMPLETE) ? ESP_OK : ESP_ERR_FIRMWARE_VERIFY_FAILED;
}

esp_err_t firmware_storage_cancel_upgrade(int session_id) {
    if (!s_ctx.initialized) return ESP_FAIL;
    
    upgrade_session_t *session = s_ctx.active_session;
    if (!session || session->id != session_id) {
        return ESP_ERR_FIRMWARE_INVALID_SESSION;
    }
    
    ESP_LOGW(TAG, "Cancelling upgrade session %d", session_id);
    
    // 关闭文件（如果打开）
    if (session->temp_file) {
        fclose(session->temp_file);
        session->temp_file = NULL;
    }
    
    // 删除临时文件
    unlink(session->temp_path);
    
    // 清理会话
    free(session);
    s_ctx.active_session = NULL;
    
    return ESP_OK;
}

bool firmware_storage_has_firmware(void) {
    struct stat st;
    if (!s_ctx.initialized) return false;
    return stat(s_ctx.firmware_path, &st) == 0;
}

esp_err_t firmware_storage_get_info(firmware_info_t *info) {
    if (!s_ctx.initialized || !info) return ESP_FAIL;
    
    if (!firmware_storage_has_firmware()) {
        return ESP_ERR_NOT_FOUND;
    }
    
    // 从文件读取信息（实际实现可能需要从文件头或单独元数据文件读取）
    memset(info, 0, sizeof(firmware_info_t));
    
    struct stat st;
    if (stat(s_ctx.firmware_path, &st) != 0) {
        return ESP_FAIL;
    }
    
    // 这里可以扩展从文件提取更多信息
    snprintf(info->version, sizeof(info->version), "unknown");
    info->total_size = st.st_size;
    info->timestamp = st.st_mtime;
    
    return ESP_OK;
}

esp_err_t firmware_storage_open_for_read(int *fd) {
    if (!s_ctx.initialized || !fd) return ESP_FAIL;
    
    if (!firmware_storage_has_firmware()) {
        return ESP_ERR_NOT_FOUND;
    }
    
    FILE *file = fopen(s_ctx.firmware_path, "rb");
    if (!file) {
        return ESP_FAIL;
    }
    
    // 使用文件指针作为"文件描述符"（简化实现）
    *fd = (int)file;
    return ESP_OK;
}

esp_err_t firmware_storage_read_data(int fd, uint8_t *buffer, size_t size, size_t *bytes_read) {
    if (!buffer || size == 0) return ESP_ERR_INVALID_ARG;
    
    FILE *file = (FILE *)fd;
    if (!file) return ESP_ERR_INVALID_ARG;
    
    size_t read = fread(buffer, 1, size, file);
    if (bytes_read) {
        *bytes_read = read;
    }
    
    return (read > 0 || size == 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t firmware_storage_close(int fd) {
    FILE *file = (FILE *)fd;
    if (!file) return ESP_ERR_INVALID_ARG;
    
    fclose(file);
    return ESP_OK;
}

esp_err_t firmware_storage_delete_firmware(void) {
    if (!s_ctx.initialized) return ESP_FAIL;
    
    if (!firmware_storage_has_firmware()) {
        return ESP_ERR_NOT_FOUND;
    }
    
    if (unlink(s_ctx.firmware_path) != 0) {
        ESP_LOGE(TAG, "Failed to delete firmware file");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Firmware file deleted");
    
    // 更新统计
    esp_spiffs_info(s_ctx.config.partition_label, 
                    &s_ctx.stats.total_space, 
                    &s_ctx.stats.used_space);
    s_ctx.stats.free_space = s_ctx.stats.total_space - s_ctx.stats.used_space;
    
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


const char *firmware_storage_get_version(void) {
    return LIB_VERSION;
}

esp_err_t spiffs_format_and_remount(void)
{
    esp_err_t ret;

    /* 1. 先卸载（如果没挂载，返回 ESP_ERR_INVALID_STATE，可以忽略） */
    ret = esp_vfs_spiffs_unregister(NULL);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "SPIFFS unregister failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 2. 格式化 SPIFFS */
    ESP_LOGW(TAG, "Formatting SPIFFS...");
    ret = esp_spiffs_format(NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS format failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 3. 重新挂载 SPIFFS */
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = NULL,
        .max_files = 5,
        .format_if_mount_failed = false,
    };

    ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS mount failed after format: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "SPIFFS formatted and mounted successfully");
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
