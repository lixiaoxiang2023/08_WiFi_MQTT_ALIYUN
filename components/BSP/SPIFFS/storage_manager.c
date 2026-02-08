#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "spi.h"
#include "esp_spiffs.h"
#include "esp_vfs.h"
#include "nvs_flash.h"
#include "storage_manager.h"


static const char               *TAG = "spiffs";


/**
 * @brief       spiffs初始化
 * @param       partition_label:分区表的分区名称
 * @param       mount_point:文件系统关联的文件路径前缀
 * @param       max_files:可以同时打开的最大文件数
 * @retval      无
 */
esp_err_t spiffs_init(char *partition_label, char *mount_point, size_t max_files)
{
    /* 配置spiffs文件系统各个参数 */
    esp_vfs_spiffs_conf_t conf = {
        .base_path = mount_point,
        .partition_label = partition_label,
        .max_files = max_files,
        .format_if_mount_failed = true,
    };

    /* 使用上面定义的设置来初始化和挂载SPIFFS文件系统 */
    esp_err_t ret_val = esp_vfs_spiffs_register(&conf);

    /* 判断SPIFFS挂载及初始化是否成功 */
    if (ret_val != ESP_OK)
    {
        if (ret_val == ESP_FAIL)
        {
            printf("Failed to mount or format filesystem\n");
        }
        else if (ret_val == ESP_ERR_NOT_FOUND)
        {
            printf("Failed to find SPIFFS partition\n");
        }
        else
        {
            printf("Failed to initialize SPIFFS (%s)\n", esp_err_to_name(ret_val));
        }

        return ESP_FAIL;
    }

    /* 打印SPIFFS存储信息 */
    size_t total = 0, used = 0;
    ret_val = esp_spiffs_info(conf.partition_label, &total, &used);

    if (ret_val != ESP_OK)
    {
        ESP_LOGI(TAG, "Failed to get SPIFFS partition information (%s)", esp_err_to_name(ret_val));
    }
    else
    {
        ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
    }

    return ret_val;
}

/**
 * @brief       注销spiffs初始化
 * @param       partition_label：分区表标识
 * @retval      无
 */
esp_err_t spiffs_deinit(char *partition_label)
{
    return esp_vfs_spiffs_unregister(partition_label);
}

// 检查文件是否存在
bool firmware_file_exists(void) {
    struct stat st;
    return (stat(FIRMWARE_FILE_PATH, &st) == 0);
}

// 获取固件文件大小
size_t get_firmware_size(void) {
    struct stat st;
    if (stat(FIRMWARE_FILE_PATH, &st) != 0) {
        return 0;
    }
    return st.st_size;
}

/**
 * @brief       程序入口
 * @param       无
 * @retval      无
 */
bool read_file_to_buffer(
    const char *path,
    uint8_t **out_buf,
    size_t *out_len
)
{
    if (!path || !out_buf || !out_len) {
        ESP_LOGE(TAG, "Invalid arguments");
        return false;
    }

    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open file: %s", path);
        return false;
    }

    // 获取文件大小
    struct stat st;
    if (stat(path, &st) != 0 || st.st_size <= 0) {
        ESP_LOGE(TAG, "Failed to get file size");
        fclose(f);
        return false;
    }

    uint8_t *buf = malloc(st.st_size);
    if (!buf) {
        ESP_LOGE(TAG, "No memory for file buffer");
        fclose(f);
        return false;
    }

    size_t read_len = fread(buf, 1, st.st_size, f);
    fclose(f);

    if (read_len != st.st_size) {
        ESP_LOGE(TAG, "File read mismatch (%zu/%ld)", read_len, (long)st.st_size);

        free(buf);
        return false;
    }

    *out_buf = buf;
    *out_len = read_len;

    ESP_LOGI(TAG, "Read file ok: %s (%d bytes)", path, read_len);
    return true;
}