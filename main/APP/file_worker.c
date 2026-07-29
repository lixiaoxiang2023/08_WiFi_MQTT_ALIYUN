#include "mqtt_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include "lwip_mqtt.h"
#include "json_processor.h"
#include "file_worker.h"
#include "web_server_handlers.h"
#include "spi_sdcard.h"

static const char *TAG = "FILE_WORK";

QueueHandle_t usb_copy_queue;

static bool get_download_storage_is_usb(void)
{
    if (read_usb_boot_config_from_nvs()) {
        ESP_LOGI(TAG, "USB功能已开启，下载目标使用 /0:");
        return true;
    }

    if (sd_spi_init() == ESP_OK) {
        ESP_LOGI(TAG, "USB功能关闭，但检测到 TF 卡，下载目标使用 /0:");
        return true;
    }

    ESP_LOGW(TAG, "USB功能关闭且 TF 卡不可用，回退到 SPIFFS");
    return false;
}

bool wait_spiffs_file_ready(const char *path, int timeout_ms)
{
    int elapsed = 0;

    while (elapsed < timeout_ms) {
        struct stat st;
        if (stat(path, &st) == 0 && st.st_size > 0) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
        elapsed += 100;
    }
    return false;
}

void file_task_worker(void *arg)
{
    char *json;
    file_task_queue = xQueueCreate(3, sizeof(char *));
    assert(file_task_queue);

    while (1) {
        if (xQueueReceive(file_task_queue, &json, portMAX_DELAY)) {

            download_url_info_t info = {0};

            mqtt_cmd_handler(json);
            
            if (huawei_cmd_handle(json)) {
                free(json);
                continue;
            }

            if (!parse_download_url_response(json, &info)) {
                free(json);
                continue;
            }

            if (info.event_type &&
                strstr(info.event_type, EVENT_UPLOAD)) {
                ESP_LOGI(TAG, "info.url: %s g_strReadLocalFileName: %s",info.url,g_strReadLocalFileName);

                obs_http_upload(info.url, g_strReadLocalFileName);

            } 
            else if (info.event_type &&
                    strstr(info.event_type, EVENT_DOWNLOAD)) {
                
                bool use_usb = get_download_storage_is_usb();
                const char *storage_root = use_usb ? USB_PATH : SPIFFS_PATH;
                char buf[128] = {0};
                snprintf(buf, sizeof(buf), "%s/%s", storage_root, info.object_name);

                strcpy(g_strWriteLocalFileName, buf);

                obs_http_download(info.url, g_strWriteLocalFileName);
                
                if (!wait_spiffs_file_ready(g_strWriteLocalFileName, 3000)) {
                    free(json);
                    continue;
                }

                if (use_usb) {
                    file_copy_msg_t msg = {0};
                    strcpy(msg.src, g_strWriteLocalFileName);

                    memset(buf,0,sizeof(buf));
                    snprintf(buf, sizeof(buf), "%s/%s", USB_PATH, info.object_name);
                    strcpy(msg.dst, buf);

                    xQueueSend(usb_copy_queue, &msg, portMAX_DELAY);
                }
            }
            free(json);
        }
    }
}