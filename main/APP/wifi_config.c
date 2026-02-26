#include "wifi_config.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_smartconfig.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include <string.h>
#include "lcd.h"
#include "cJSON.h"

#ifdef STA_AP_MODE
    #include "web_server.h"
#endif

static const char *TAG = "wifi_config";
static bool s_ap_config_mode = true;   // 🔒 AP 配网页面在线
static bool s_sta_connecting = false;

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
#ifdef STA_AP_MODE
            web_server_start();
#endif
            break;

        case WIFI_EVENT_STA_DISCONNECTED:
            ESP_LOGW(TAG, "WiFi disconnected");
            if (s_ap_config_mode) {
                ESP_LOGI(TAG, "AP config mode, skip STA reconnect");
                break;
            }
            esp_wifi_connect();
            break;

        default:
            break;
        }
    }

    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG, "Got IP");
        s_sta_connecting = false;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CFG_CONNECTED_BIT);
    }

    else if (event_base == SC_EVENT) {

        switch (event_id) {

        case SC_EVENT_GOT_SSID_PSWD: {
            smartconfig_event_got_ssid_pswd_t *evt =
                (smartconfig_event_got_ssid_pswd_t *)event_data;

            ESP_LOGI(TAG, "SmartConfig SSID: %s", evt->ssid);

            wifi_apply_config(
                (const char *)evt->ssid,
                (const char *)evt->password
            );
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


esp_err_t wifi_apply_config(const char *ssid, const char *password)
{
    wifi_config_t cfg = {0};

    strncpy((char *)cfg.sta.ssid, ssid, sizeof(cfg.sta.ssid));
    strncpy((char *)cfg.sta.password, password, sizeof(cfg.sta.password));

    ESP_LOGI(TAG, "Apply WiFi: %s", ssid);

    s_ap_config_mode = false;     // 🔓 解锁 STA
    s_sta_connecting = true;

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));

    esp_err_t err = esp_wifi_connect();
    if (err == ESP_ERR_WIFI_CONN) {
        ESP_LOGI(TAG, "Already connecting");
        return ESP_OK;
    }

    return err;
}



/* ================= WiFi 启动入口（AP + STA） ================= */
esp_err_t wifi_smartconfig_sta(void)
{
    esp_err_t ret;

    s_wifi_event_group = xEventGroupCreate();
    if (!s_wifi_event_group) {
        return ESP_ERR_NO_MEM;
    }

    /* ---------- 网络栈 ---------- */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    //STA+AP模式配置
#ifdef STA_AP_MODE
    /* ---------- 创建 STA & AP ---------- */
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();
#else
    esp_netif_create_default_wifi_sta();
#endif
    /* ---------- WiFi 初始化 ---------- */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    /* ---------- 事件注册 ---------- */
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(SC_EVENT, ESP_EVENT_ANY_ID, event_handler, NULL));
#ifdef STA_AP_MODE
    /* ---------- AP 配置 ---------- */
    wifi_config_t ap_cfg = {
        .ap = {
            .ssid = "ESP32_Config",
            .password = "12345678",
            .channel = 1,      // 🔥 必须固定
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA2_PSK
        }
    };


    /* ---------- STA 模式 ---------- */
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
#else
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
#endif
    /* ---------- 启动 WiFi ---------- */
    ESP_ERROR_CHECK(esp_wifi_start());

    /* ---------- 判断是否已有 WiFi ---------- */
    wifi_config_t wifi_cfg;
    ret = esp_wifi_get_config(WIFI_IF_STA, &wifi_cfg);

    if (ret == ESP_OK && strlen((char *)wifi_cfg.sta.ssid) > 0) {
        ESP_LOGI(TAG, "Found saved WiFi: %s", wifi_cfg.sta.ssid);
        esp_wifi_connect();

        char ssid_str[64] = {0};
        snprintf(ssid_str, sizeof(ssid_str), "SSID: %s", wifi_cfg.sta.ssid);
        lcd_show_string(30, 70, 200, 16, 16, ssid_str, RED);

    } else {
        ESP_LOGW(TAG, "No WiFi config, start SmartConfig");
        lcd_show_string(30, 70, 200, 16, 16, "AP Config Mode", RED);
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

#define MAX_SCAN_RESULTS 20
typedef struct {
    char ssid[33];  // SSID + '\0'
    int8_t rssi;
} scan_result_t;

static scan_result_t g_scan_results[MAX_SCAN_RESULTS];
static int g_scan_count = 0;
static bool scan_done = false;

// 扫描完成回调
static void wifi_scan_done_cb(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    uint16_t ap_num = 0;
    esp_wifi_scan_get_ap_num(&ap_num);
    if (ap_num > MAX_SCAN_RESULTS) ap_num = MAX_SCAN_RESULTS;

    wifi_ap_record_t ap_records[MAX_SCAN_RESULTS];
    esp_wifi_scan_get_ap_records(&ap_num, ap_records);

    g_scan_count = 0;
    for (int i = 0; i < ap_num; i++) {
        bool found = false;
        for (int j = 0; j < g_scan_count; j++) {
            if (strcmp((char*)ap_records[i].ssid, g_scan_results[j].ssid) == 0) {
                found = true;
                if (ap_records[i].rssi > g_scan_results[j].rssi)
                    g_scan_results[j].rssi = ap_records[i].rssi;
                break;
            }
        }
        if (!found) {
            strncpy(g_scan_results[g_scan_count].ssid, (char*)ap_records[i].ssid, 32);
            g_scan_results[g_scan_count].ssid[32] = 0;
            g_scan_results[g_scan_count].rssi = ap_records[i].rssi;
            g_scan_count++;
        }
    }
    scan_done = true;
}

// HTTP Handler 返回 JSON
esp_err_t wifi_scan_handler(httpd_req_t *req)
{
    scan_done = false;

    wifi_scan_config_t cfg = {0};
    esp_wifi_scan_start(&cfg, false); // 异步扫描
    esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_SCAN_DONE, &wifi_scan_done_cb, NULL);

    // 等扫描完成（最长 3 秒）
    int wait_ms = 0;
    while (!scan_done && wait_ms < 3000) {
        vTaskDelay(pdMS_TO_TICKS(100));
        wait_ms += 100;
    }

    // 构建 JSON
    cJSON *root = cJSON_CreateArray();
    for (int i = 0; i < g_scan_count; i++) {
        char buf[48];
        snprintf(buf, sizeof(buf), "%.32s (%ddBm)", g_scan_results[i].ssid, g_scan_results[i].rssi);
        cJSON_AddItemToArray(root, cJSON_CreateString(buf));
    }

    const char* json_string = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_string, HTTPD_RESP_USE_STRLEN);

    cJSON_Delete(root);
    free((void*)json_string);

    return ESP_OK;
}

typedef struct {
    char local_file[64];
    char upload_server[64];
} data_config_t;

data_config_t g_data_config;

esp_err_t save_config_handler(httpd_req_t *req)
{
    char buf[128];
    int ret = httpd_req_recv(req, buf, req->content_len);
    if (ret <= 0) return ESP_FAIL;
    buf[ret] = 0;

    cJSON *json = cJSON_Parse(buf);
    if (!json) return ESP_FAIL;

    const cJSON *local = cJSON_GetObjectItem(json, "localFile");
    const cJSON *server = cJSON_GetObjectItem(json, "uploadServer");

    if (local && server) {
        strncpy(g_data_config.local_file, local->valuestring, sizeof(g_data_config.local_file)-1);
        strncpy(g_data_config.upload_server, server->valuestring, sizeof(g_data_config.upload_server)-1);
    }
    ESP_LOGI(TAG, "local: %s upload_server: %s",g_data_config.local_file,g_data_config.upload_server);

    cJSON_Delete(json);
    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}