#include "mqtt_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include "lwip_mqtt.h"
#include "json_processor.h"
#include "file_worker.h"

static const char *TAG = "FILE_WORK";

QueueHandle_t usb_copy_queue;

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
                
                char buf[64]={0};
                memset(buf,0,sizeof(buf));
                snprintf(buf, sizeof(buf), "%s/%s", SPIFFS_PATH, info.object_name);

                strcpy(g_strWriteLocalFileName, buf);

                obs_http_download(info.url, g_strWriteLocalFileName);
                
                if (!wait_spiffs_file_ready(g_strWriteLocalFileName, 3000)) {
                    free(json);
                    continue;
                }

                file_copy_msg_t msg = {0};
                strcpy(msg.src, g_strWriteLocalFileName);

                memset(buf,0,sizeof(buf));
                snprintf(buf, sizeof(buf), "%s/%s", USB_PATH, info.object_name);
                strcpy(msg.dst, buf);

                xQueueSend(usb_copy_queue, &msg, portMAX_DELAY);
            }

            free(json);
        }
    }
}