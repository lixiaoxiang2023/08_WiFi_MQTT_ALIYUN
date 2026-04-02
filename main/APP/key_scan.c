#include "mqtt_client.h"
#include "xl9555.h"
#include "freertos/FreeRTOS.h" // Keep this one for FreeRTOS
#include "freertos/task.h"    // Keep this one for FreeRTOS
#include <string.h>
#include <stdio.h>
#include "lwip_mqtt.h"
#include "wifi_config.h"


esp_mqtt_client_handle_t client = NULL;

typedef enum {
    WIFI_MODE_WEB_CONFIG,   // APSTA
    WIFI_MODE_STA_ONLY      // 纯 STA
} wifi_user_mode_t;

static wifi_user_mode_t g_wifi_user_mode = WIFI_MODE_STA_ONLY;

void wifi_switch_mode(void)
{
    ESP_LOGI("WIFI", "Switching WiFi mode from %d...", g_wifi_user_mode);

    // 1️⃣ 安全停止并销毁 MQTT
    if (client) {
        ESP_LOGI("WIFI", "Stopping MQTT client...");
        esp_mqtt_client_stop(client);
        // 给一点时间让 MQTT 线程退出循环
        vTaskDelay(pdMS_TO_TICKS(100)); 
        esp_mqtt_client_destroy(client);
        client = NULL;
    }

    // 2️⃣ 停止 WiFi 驱动
    // 注意：stop 会清空当前的连接状态
    esp_err_t err = esp_wifi_stop();
    if (err != ESP_OK) {
        ESP_LOGE("WIFI", "Failed to stop wifi: %s", esp_err_to_name(err));
    }

    // 3️⃣ 根据逻辑切换模式
    if (g_wifi_user_mode == WIFI_MODE_WEB_CONFIG)
    {
        // 目标：纯客户端模式 (STA)
        ESP_LOGI("WIFI", "Switching to [STA ONLY] Mode");
        //lcd_clear_line(110); // 建议封装个清行函数
        lcd_show_string(30, 110, 200, 16, 16, "Mode: STA", RED);

        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        g_wifi_user_mode = WIFI_MODE_STA_ONLY;
    }
    else
    {
        // 目标：配网模式 (AP + STA)
        ESP_LOGI("WIFI", "Switching to [AP + STA] Mode");
        lcd_show_string(30, 110, 200, 16, 16, "Mode: AP+STA", RED);
        lcd_show_string(30, 130, 200, 16, 16, "IP: 192.168.4.1", RED);

        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
        
        // 启动 Web 服务前，建议在内部先 stop 掉旧的防止重叠
        web_prov_start(); 
        g_wifi_user_mode = WIFI_MODE_WEB_CONFIG;
    }

    // 4️⃣ 重新启动 WiFi
    ESP_ERROR_CHECK(esp_wifi_start());

    // 5️⃣ 核心连接逻辑
    //if (g_wifi_user_mode == WIFI_MODE_STA_ONLY)
    {
    //    ESP_LOGI("WIFI", "Connecting to AP...");
        esp_wifi_connect(); 
    }
}


// Static handle to the OTA update task, initialized to NULL
static TaskHandle_t xOtaTaskHandle = NULL;

// New task function to handle OTA update process
static void ota_update_task(void *arg)
{
    printf("Starting OTA update task...\n");
    login_response_t resp = {0};

    if (http_login(&resp)) {
        ESP_LOGI("MAIN", "登录成功, token=%s", resp.token);
        // ⭐ 1. 登录成功 → 请求版本
        if (http_get_version(resp.token)) {
            char buf[256] = {0};
            // 拼接 U 盘完整路径 (例如: /usb/ota_data_initial.bin)
            snprintf(buf, sizeof(buf), "%s/%s", USB_PATH, g_download_info.version);
            strcpy(g_strWriteLocalFileName, buf);
            ESP_LOGI("MAIN", "g_download_info.file_name :%s",g_download_info.version);

            // ⭐ 2. 开始下载到 U 盘
            if (download_to_usb(g_download_info.url, g_strWriteLocalFileName) == ESP_OK) {
                ESP_LOGI("MAIN", "下载完成，开始 MD5 校验...");

                // ⭐ 3. 进行 MD5 完整性校验
                if (verify_file_md5(g_strWriteLocalFileName, g_download_info.md5)) {
                    ESP_LOGI("MAIN", "✅ MD5 校验通过，固件合法！");
                    
                    // --- 此处可以安全地执行后续 OTA 处理 (如从 U 盘刷机) ---
                    // execute_ota_update_from_usb(g_strWriteLocalFileName);
                    
                } else {
                    ESP_LOGE("MAIN", "❌ MD5 校验失败，文件可能在传输中损坏！");
                    
                    // 校验失败，建议删除损坏的文件，避免占用空间或被误用
                    unlink(g_strWriteLocalFileName); 
                }
            } else {
                ESP_LOGE("MAIN", "下载失败，请检查网络或 U 盘挂载状态");
            }
        }
    } 
    else 
    {
        ESP_LOGE("MAIN", "登录失败");
    }

    printf("OTA update task finished.\n");
    xOtaTaskHandle = NULL; // Clear the task handle as the task is about to delete itself
    vTaskDelete(NULL); // Delete this task
}


void key_scan_task(void *arg)
{
    uint8_t key;

    while(1)
    {
        key = xl9555_key_scan(0);
        switch (key)
        {
            case KEY0_PRES:
            {
                ESP_LOGI("MAIN", ">>>>>> 开始全量数据同步流程 <<<<<<");
                login_response_t login_resp = {0};

                // 1. 登录获取 Token
                if (!http_login(&login_resp)) {
                    ESP_LOGE("MAIN", "步骤1失败: 登录鉴权未通过");
                    break; 
                }
                const char* token = login_resp.token;

                // 2. 获取产品列表 (Products)
                if (http_get_all_products(token)) {
                    ESP_LOGI("MAIN", "步骤2成功: 已获取产品列表");
                    // 这里建议立即处理 g_http_resp.buffer，比如解析出产品ID或存入其他变量
                } else {
                    ESP_LOGE("MAIN", "步骤2失败: 产品列表拉取异常");
                }

                // 3. 获取平台列表 (Platforms)
                if (http_get_product_platforms(token,4)) {
                    ESP_LOGI("MAIN", "步骤3成功: 已获取平台列表");
                } else {
                    ESP_LOGE("MAIN", "步骤3失败: 平台列表拉取异常");
                }

                // 4. 获取版本列表 (Versions)
                if (http_get_platform_versions(token,5)) {
                    ESP_LOGI("MAIN", "步骤4成功: 已获取版本列表");
                } else {
                    ESP_LOGE("MAIN", "步骤4失败: 版本列表拉取异常");
                }

                ESP_LOGI("MAIN", ">>>>>> 同步流程结束，数据已更新至全局缓存 <<<<<<");
                break;
            }
            case KEY1_PRES:
            {
                printf("KEY1 has been pressed \n");

                // If the OTA task is not already running, create it
                if (xOtaTaskHandle == NULL) {
                    xTaskCreate(ota_update_task,     // Task function
                                "OTA_Update_Task",   // Name of task
                                4096,                // Stack size (bytes) - adjust as needed
                                NULL,                // Parameter to pass to the task
                                5,                   // Priority (lower than key_scan_task if key_scan is critical)
                                &xOtaTaskHandle);    // Task handle to keep track
                    printf("Starting OTA update in background task.\n");
                } else {
                    printf("OTA update is already running. Please wait.\n");
                }
                break;
            }
            case KEY2_PRES:
            {
                printf("KEY2 has been pressed \n");
                //LED(0);
                break;
            }
            case KEY3_PRES:
            {
                printf("KEY3 pressed -> Switch WiFi mode\n");
                wifi_switch_mode();
                break;
            }
            default:
            {
                break;
            }
        }
        vTaskDelay(10);
    }
}
