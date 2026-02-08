#ifndef __STORAGE_MANAGER_H__
#define __STORAGE_MANAGER_H__

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DEFAULT_FD_NUM          5
#define DEFAULT_MOUNT_POINT     "/spiffs"
#define WRITE_DATA              "ALIENTEK ESP32-S3\r\n"

#define SPIFFS_BASE_PATH          "/spiffs"
#define SPIFFS_PARTITION_LABEL    "storage"
#define FIRMWARE_FILE_PATH        "/spiffs/firmware.bin"
#define FIRMWARE_TEMP_PATH        "/spiffs/firmware.tmp"

extern esp_err_t spiffs_init(char *partition_label, char *mount_point, size_t max_files);
extern esp_err_t spiffs_deinit(char *partition_label);

// 检查固件文件是否存在
extern bool firmware_file_exists(void);

// 获取固件文件大小
extern size_t get_firmware_size(void);

// 获取存储信息
extern esp_err_t get_storage_info(size_t* total, size_t* used);
bool read_file_to_buffer(
    const char *path,
    uint8_t **out_buf,
    size_t *out_len
);
#ifdef __cplusplus
}
#endif

#endif /* __STORAGE_MANAGER_H__ */