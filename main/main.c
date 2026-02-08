/**
 ****************************************************************************************************
* @file        main.c
* @author      正点原子团队(ALIENTEK)
* @version     V1.0
* @date        2023-08-26
* @brief       WIFI Aliyun实验
* @license     Copyright (c) 2020-2032, 广州市星翼电子科技有限公司
****************************************************************************************************
* @attention
*
* 实验平台:正点原子 ESP32-S3 开发板
* 在线视频:www.yuanzige.com
* 技术论坛:www.openedv.com
* 公司网址:www.alientek.com
* 购买地址:openedv.taobao.com
*
****************************************************************************************************
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_system.h"
#include "nvs_flash.h"
#include "led.h"
#include "lcd.h"
#include "wifi_config.h"
#include "lwip_demo.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_sntp.h"
#include "esp_netif_sntp.h"
#include "spi_sdcard.h"
#include "storage_manager.h"
#include "firmware_storage.h"
#include "json_processor.h"
#include "tud_flash.h"
#include "huawei_ota.h"
#include "esp_ota_ops.h"
//#include "web_server.h"


static const char *TAG = "MAIN";
i2c_obj_t i2c0_master;

void print_current_time(void) {
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    char strftime_buf[64];
    strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
    ESP_LOGI(TAG, "Current time: %s", strftime_buf);
}

void initialize_sntp_v5(void) {
    ESP_LOGI(TAG, "Using ESP-IDF v5.0+ SNTP API");
    
    // 设置时区
    setenv("TZ", "CST-8", 1);
    tzset();
    
    // 使用新的 API
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(
        3,  // 服务器数量
        ESP_SNTP_SERVER_LIST("pool.ntp.org", "time1.cloud.tencent.com", "ntp.aliyun.com")
    );
    
    config.start = true;
    config.server_from_dhcp = false;
    config.renew_servers_after_new_IP = true;
    config.index_of_first_server = 0;
    config.ip_event_to_renew = IP_EVENT_STA_GOT_IP;
    
    esp_netif_sntp_init(&config);
    
    // 等待同步
    if (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(30000)) != ESP_OK) {
        ESP_LOGW(TAG, "Failed to synchronize time within 30s");
    } else {
        ESP_LOGI(TAG, "Time synchronized successfully");
        print_current_time();
    }
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


typedef struct {
    char src[64];
    char dst[64];
} file_copy_msg_t;

static QueueHandle_t usb_copy_queue;
void usb_copy_task(void *arg)
{
    file_copy_msg_t msg;

    while (1) {
        if (xQueueReceive(usb_copy_queue, &msg, portMAX_DELAY)) {

            ESP_LOGI("USB", "copy %s -> %s", msg.src, msg.dst);

            FILE *src = fopen(msg.src, "rb");
            if (!src) {
                ESP_LOGE("USB", "open src failed");
                continue;
            }

            FILE *dst = fopen(msg.dst, "wb");
            if (!dst) {
                ESP_LOGE("USB", "open dst failed");
                fclose(src);
                continue;
            }

            uint8_t buf[1024];
            size_t len;

            while ((len = fread(buf, 1, sizeof(buf), src)) > 0) {
                if (fwrite(buf, 1, len, dst) != len) {
                    ESP_LOGE("USB", "write failed");
                    break;
                }
                vTaskDelay(1); // 喂狗 + 让 USB 跑
            }

            fflush(dst);
            fclose(dst);
            fclose(src);

            ESP_LOGI("USB", "copy done");

            /* ========= 核心：强制主机重新识别 ========= */
            ESP_LOGW("USB", "re-enumerate USB MSC");

            tud_disconnect();
            vTaskDelay(pdMS_TO_TICKS(800));
            tud_connect();
        }
    }
}


void file_task_worker(void *arg)
{
    char *json;
    
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

void ota_check_and_confirm(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();

    esp_ota_img_states_t ota_state;
    esp_err_t err = esp_ota_get_state_partition(running, &ota_state);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "OTA state = %d", ota_state);

        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
            ESP_LOGI(TAG, "First boot after OTA, mark valid");
            esp_ota_mark_app_valid_cancel_rollback();
        }
    } else {
        ESP_LOGE(TAG, "Error getting OTA state: %s", esp_err_to_name(err));
    }
}


void app_main(void)
{
    esp_err_t ret;
    char logo_str[64]= {0};

    ret = nvs_flash_init();
     //   ESP_ERROR_CHECK(nvs_flash_erase());

    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    
    ota_read_success_flag();
    ota_check_and_confirm();

    led_init();
    i2c0_master = iic_init(I2C_NUM_0);
    spi2_init();
    xl9555_init(i2c0_master);
    lcd_init();

    snprintf(logo_str, sizeof(logo_str), "Version: %s", FW_VERSION);
    lcd_show_string(30, 50, 200, 16, 16, logo_str, RED);
    //lcd_show_string(30, 70, 200, 16, 16, "SD TEST", RED);
    //lcd_show_string(30, 90, 200, 16, 16, "ATOM@ALIENTEK", RED);
    ESP_LOGI("MAIN", "soft version: %s",FW_VERSION);
    firmware_storage_check(NULL);
    tud_usb_flash();

    usb_copy_queue = xQueueCreate(2, sizeof(file_copy_msg_t));
    file_task_queue = xQueueCreate(3, sizeof(char *));
    assert(file_task_queue);

    xTaskCreate(file_task_worker, "file_task_worker", 6 * 1024, NULL, 5, NULL);
    xTaskCreatePinnedToCore(
        usb_copy_task,
        "usb_copy",
        4096,
        NULL,
        4,
        NULL,
        0
    );

    wifi_smartconfig_sta();
    wifi_config_wait_connected();
    //web_server_start();
    initialize_sntp_v5();
    
    lwip_demo();              
}
