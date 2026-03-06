#include "mqtt_client.h"
#include "xl9555.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
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
    ESP_LOGI("WIFI", "Switching WiFi mode...");

    // 1️⃣ 先停止 MQTT
    if (client) {
        esp_mqtt_client_stop(client);
        esp_mqtt_client_destroy(client);
        client = NULL;
    }

    // 2️⃣ 停止 WiFi
    esp_wifi_stop();

    if (g_wifi_user_mode == WIFI_MODE_WEB_CONFIG)
    {
        // 切到 STA_ONLY
        ESP_LOGI("WIFI", "Switch to STA ONLY");
        lcd_show_string(30, 110, 200, 16, 16, "STA      ", RED);
        lcd_show_string(30, 130, 200, 16, 16, "                          ", RED);

        esp_wifi_set_mode(WIFI_MODE_STA);
        g_wifi_user_mode = WIFI_MODE_STA_ONLY;
    }
    else
    {
        // 切到 APSTA
        ESP_LOGI("WIFI", "Switch to APSTA");
        lcd_show_string(30, 110, 200, 16, 16, "AP+STA", RED);
        lcd_show_string(30, 130, 200, 16, 16, "Web service:192.168.4.1", RED);

        esp_wifi_set_mode(WIFI_MODE_APSTA);
        web_prov_start();
        g_wifi_user_mode = WIFI_MODE_WEB_CONFIG;
    }

    // 3️⃣ 重启 WiFi
    esp_wifi_start();

    // 4️⃣ 如果是 STA_ONLY，则重启 MQTT
    if (g_wifi_user_mode == WIFI_MODE_STA_ONLY)
    {
        esp_wifi_connect();   // ⭐ 必须加
       // mqtt_init();  // 或单独写个 mqtt_start()
    }
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
                printf("KEY0 has been pressed \n");
                if(client)
                {
                    memset(g_strReadLocalFileName,0,sizeof(g_strReadLocalFileName));
                    // strcpy(g_strReadLocalFileName,SPIFFS_FILE_NAME);
                    strcat(g_strReadLocalFileName, USB_PATH);                
                    strcat(g_strReadLocalFileName, "/");
                    strcat(g_strReadLocalFileName, g_data_config.local_file);
                    send_json_data_events(client,DEVICE_PUBLISH_EVENT,EVENT_UPLOAD,g_data_config.upload_server);
                }
                break;
            }
            case KEY1_PRES:
            {
                printf("KEY1 has been pressed \n");
                
                if(client)
                {
                    send_json_data_events(client,DEVICE_PUBLISH_EVENT,EVENT_DOWNLOAD,OBS_DOWN_FILE_NAME);
                }
                //xl9555_pin_write(BEEP_IO, 1);
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
