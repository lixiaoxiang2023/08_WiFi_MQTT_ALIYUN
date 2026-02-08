#include "wifi_config.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_smartconfig.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include <string.h>

static const char *TAG = "wifi_config";

static EventGroupHandle_t s_wifi_event_group;
static bool s_smartconfig_started = false;

static void smartconfig_task(void *parm);
static void event_handler(void *arg,
                          esp_event_base_t event_base,
                          int32_t event_id,
                          void *event_data);

/* ================= 事件处理 ================= */
static void event_handler(void *arg,
                          esp_event_base_t event_base,
                          int32_t event_id,
                          void *event_data)
{
    if (event_base == WIFI_EVENT) {

        switch (event_id) {

        case WIFI_EVENT_STA_START:
            ESP_LOGI(TAG, "WIFI_EVENT_STA_START");
            esp_wifi_connect();
            break;

        case WIFI_EVENT_STA_DISCONNECTED:
            ESP_LOGW(TAG, "WiFi disconnected, retry...");
            esp_wifi_connect();
            xEventGroupClearBits(s_wifi_event_group, WIFI_CFG_CONNECTED_BIT);
            break;

        default:
            break;
        }
    }

    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG, "Got IP");
        xEventGroupSetBits(s_wifi_event_group, WIFI_CFG_CONNECTED_BIT);
    }

    else if (event_base == SC_EVENT) {

        switch (event_id) {

        case SC_EVENT_GOT_SSID_PSWD: {
            smartconfig_event_got_ssid_pswd_t *evt =
                (smartconfig_event_got_ssid_pswd_t *)event_data;

            wifi_config_t wifi_config = {0};
            memcpy(wifi_config.sta.ssid, evt->ssid, sizeof(wifi_config.sta.ssid));
            memcpy(wifi_config.sta.password, evt->password, sizeof(wifi_config.sta.password));

            ESP_LOGI(TAG, "SmartConfig SSID: %s", wifi_config.sta.ssid);

            esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
            esp_wifi_connect();
            break;
        }

        case SC_EVENT_SEND_ACK_DONE:
            ESP_LOGI(TAG, "SmartConfig Done");
            xEventGroupSetBits(s_wifi_event_group, WIFI_CFG_SC_DONE_BIT);
            break;

        default:
            break;
        }
    }
}

/* ================= WiFi 启动入口 ================= */
esp_err_t wifi_smartconfig_sta(void)
{
    esp_err_t ret;

    /* ---------- NVS ---------- */
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    s_wifi_event_group = xEventGroupCreate();
    if (!s_wifi_event_group) {
        return ESP_ERR_NO_MEM;
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(SC_EVENT, ESP_EVENT_ANY_ID, event_handler, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    /* ---------- 判断是否已有 WiFi ---------- */
    wifi_config_t wifi_cfg;
    ret = esp_wifi_get_config(WIFI_IF_STA, &wifi_cfg);

    ESP_ERROR_CHECK(esp_wifi_start());

    if (ret == ESP_OK && strlen((char *)wifi_cfg.sta.ssid) > 0) {
        ESP_LOGI(TAG, "Found saved WiFi: %s", wifi_cfg.sta.ssid);
        esp_wifi_connect();
    } else {
        ESP_LOGW(TAG, "No WiFi config, start SmartConfig");
        xTaskCreate(smartconfig_task, "smartconfig_task", 4096, NULL, 3, NULL);
    }

    return ESP_OK;
}

/* ================= 等待连接 ================= */
void wifi_config_wait_connected(void)
{
    xEventGroupWaitBits(
        s_wifi_event_group,
        WIFI_CFG_CONNECTED_BIT,
        false,
        true,
        portMAX_DELAY
    );
}

EventGroupHandle_t wifi_config_get_event_group(void)
{
    return s_wifi_event_group;
}

/* ================= SmartConfig 任务 ================= */
static void smartconfig_task(void *parm)
{
    if (s_smartconfig_started) {
        vTaskDelete(NULL);
        return;
    }

    s_smartconfig_started = true;

    ESP_ERROR_CHECK(esp_smartconfig_set_type(SC_TYPE_ESPTOUCH));
    smartconfig_start_config_t cfg = SMARTCONFIG_START_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_smartconfig_start(&cfg));

    while (1) {
        EventBits_t bits = xEventGroupWaitBits(
            s_wifi_event_group,
            WIFI_CFG_CONNECTED_BIT | WIFI_CFG_SC_DONE_BIT,
            false,
            false,
            portMAX_DELAY
        );

        if (bits & WIFI_CFG_CONNECTED_BIT) {
            ESP_LOGI(TAG, "WiFi Connected");
        }

        if (bits & WIFI_CFG_SC_DONE_BIT) {
            esp_smartconfig_stop();
            s_smartconfig_started = false;
            vTaskDelete(NULL);
        }
    }
}
