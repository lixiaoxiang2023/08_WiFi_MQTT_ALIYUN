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
#include <dirent.h>     // ← 必须加这个
#include <sys/stat.h>   // ← 建议加这个
#include "lwip_mqtt.h"
#include "web_server.h"
#include "esp_netif_sntp.h"
#include "esp_mac.h"
#include "web_server_handlers.h"
#include"key_scan.h"

#define WIFI_CONNECT_TIMEOUT_MS 8000   // 8秒超时
#define WIFI_CONNECT_RETRY_MAX 3        // STA 最大重试次数
static int s_retry_count = 0;  // STA 重试计数
static const char *TAG = "wifi_config";
static bool s_ap_config_mode = true;   // 🔒 AP 配网页面在线
static bool s_sta_connecting = false;

typedef enum {
    WIFI_PROV_NONE = 0,
    WIFI_PROV_SMARTCONFIG,
    WIFI_PROV_WEB
} wifi_prov_mode_t;

static wifi_prov_mode_t s_prov_mode = WIFI_PROV_NONE;

EventGroupHandle_t s_wifi_event_group;
static bool s_smartconfig_started = false;
static void smartconfig_start(void);
static void smartconfig_task(void *parm);
void web_prov_start(void);
static void smartconfig_stop(void);
static void event_handler(void *arg,
                          esp_event_base_t event_base,
                          int32_t event_id,
                          void *event_data);

static void smartconfig_start(void)
{
    if (s_prov_mode == WIFI_PROV_SMARTCONFIG) {
        ESP_LOGW(TAG, "SmartConfig already running");
        return;
    }

    ESP_LOGI(TAG, "Start SmartConfig");

    s_prov_mode = WIFI_PROV_SMARTCONFIG;
    s_smartconfig_started = true;

    ESP_ERROR_CHECK(esp_smartconfig_set_type(SC_TYPE_ESPTOUCH));
    smartconfig_start_config_t cfg = SMARTCONFIG_START_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_smartconfig_start(&cfg));
}

void web_prov_start(void)
{
    ESP_LOGI(TAG, "Switch to WEB Provisioning");

    // 🔥 先停止 SmartConfig
    smartconfig_stop();

    s_prov_mode = WIFI_PROV_WEB;
    s_ap_config_mode = true;
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
    web_server_start();
}

void web_prov_stop(void)
{
    web_server_stop();
}

static void smartconfig_stop(void)
{
    if (s_prov_mode != WIFI_PROV_SMARTCONFIG)
        return;

    ESP_LOGI(TAG, "Stop SmartConfig");

    esp_smartconfig_stop();
    s_smartconfig_started = false;
    s_prov_mode = WIFI_PROV_NONE;
}
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
          //  web_server_start();
            break;

        case WIFI_EVENT_STA_DISCONNECTED:
            ESP_LOGW(TAG, "WiFi disconnected");

            if(s_retry_count < WIFI_CONNECT_RETRY_MAX){
                s_retry_count++;
                ESP_LOGI(TAG, "Retrying WiFi connect %d/%d", s_retry_count, WIFI_CONNECT_RETRY_MAX);
                esp_smartconfig_stop(); // Disconnect and then re-connect, not start smartconfig
                esp_wifi_connect();
            } else {
                ESP_LOGW(TAG, "Exceeded max retries, will start SmartConfig");
                // 触发主任务启动 SmartConfig （如果需要）
                // xEventGroupSetBits(s_wifi_event_group, WIFI_CFG_CONNECTED_BIT); // 这一行可能不准确，看你的逻辑
            }
            break;

        case WIFI_EVENT_AP_STACONNECTED: // AP 客户端连接
            ESP_LOGI(TAG, "station "MACSTR "join, AID=%d",
                     MAC2STR(((wifi_event_ap_staconnected_t*)event_data)->mac),
                     ((wifi_event_ap_staconnected_t*)event_data)->aid);
            break;

        case WIFI_EVENT_AP_STADISCONNECTED: // AP 客户端断开
            ESP_LOGI(TAG, "station "MACSTR "leave, AID=%d",
                     MAC2STR(((wifi_event_ap_stadisconnected_t*)event_data)->mac),
                     ((wifi_event_ap_stadisconnected_t*)event_data)->aid);
            break;

        default:
            break;
        }
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG, "Got IP");
      //  smartconfig_stop();
        s_sta_connecting = false;
        s_retry_count = 0;
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
            smartconfig_stop();
            break;

        default:
            break;
        }
    }
}


esp_err_t wifi_apply_config(const char *ssid, const char *password)
{
    wifi_config_t cfg = {0};

    strncpy((char *)cfg.sta.ssid, ssid, sizeof(cfg.sta.ssid) - 1); // -1 for null terminator
    cfg.sta.ssid[sizeof(cfg.sta.ssid) - 1] = '\0';
    strncpy((char *)cfg.sta.password, password, sizeof(cfg.sta.password) - 1); // -1 for null terminator
    cfg.sta.password[sizeof(cfg.sta.password) - 1] = '\0';

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

    /* ---------- 创建 STA & AP ---------- */
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    /* ---------- WiFi 初始化 ---------- */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    /* ---------- 事件注册 ---------- */
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(SC_EVENT, ESP_EVENT_ANY_ID, event_handler, NULL));

    vTaskDelay(pdMS_TO_TICKS(100)); // STA 启动缓冲
     ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA)); // 不在这里设置模式，web_prov_start 或 smartconfig_start 会设置

    /* ---------- 启动 WiFi ---------- */
     ESP_ERROR_CHECK(esp_wifi_start()); // 不在这里启动，web_prov_start 或 smartconfig_start 会启动

    /* ---------- 判断是否已有 WiFi ---------- */
    wifi_config_t wifi_cfg;
    ret = esp_wifi_get_config(WIFI_IF_STA, &wifi_cfg);

    if (ret == ESP_OK && strlen((char *)wifi_cfg.sta.ssid) > 0) {

        ESP_LOGI(TAG, "Found saved WiFi: %s", wifi_cfg.sta.ssid);

        s_prov_mode = WIFI_PROV_NONE;
        s_retry_count = 0;

        esp_wifi_connect();

        // 🔥 等待连接成功或超时
        EventBits_t bits = xEventGroupWaitBits(
            s_wifi_event_group,
            WIFI_CFG_CONNECTED_BIT,
            pdFALSE,
            pdTRUE,
            pdMS_TO_TICKS(WIFI_CONNECT_TIMEOUT_MS*WIFI_CONNECT_RETRY_MAX)
        );

        if (bits & WIFI_CFG_CONNECTED_BIT) {

            wifi_ap_record_t ap_info;
            if(esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK){
                ESP_LOGI(TAG, "WiFi connected successfully, RSSI: %d", ap_info.rssi);
            } else {
                ESP_LOGI(TAG, "WiFi connected successfully");
            }


        } else {

            ESP_LOGW(TAG, "WiFi connect failed, start SmartConfig");

           // s_prov_mode = WIFI_PROV_SMARTCONFIG;
           // xTaskCreate(smartconfig_task, "smartconfig_task", 4096, NULL, 5, NULL);
            xTaskCreatePinnedToCore(
                smartconfig_task,
                "smartconfig_task",
                4096,
                NULL,
                5,
                NULL,
                1
                );
        }
    } 
    return ESP_OK;
}


/* ================= 等待连接 ================= */
esp_err_t wifi_config_wait_connected(void)
{
    ESP_LOGI("WIFI", "等待 WiFi 连接...");

    // 等待连接位或错误位（建议增加一个 FAIL_BIT）
    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_event_group,
        WIFI_CFG_CONNECTED_BIT, // 你可以顺便加上错误位，如 | WIFI_FAIL_BIT
        pdFALSE,                // 执行完不清除位
        pdTRUE,                 // 等待所有位（如果只有一个位则无所谓）
        portMAX_DELAY    // ⭐ 优化点：设置30秒超时，不要死等
    );
    // 修复点：必须根据等待结果返回 bool 值
    if (bits & WIFI_CFG_CONNECTED_BIT) {
        ESP_LOGI("WIFI", "连接成功");
        return true; 
    } else {
        ESP_LOGE("WIFI", "连接超时或失败");
        return false;
    }
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
    //ESP_ERROR_CHECK(esp_smartconfig_start(&cfg));
    smartconfig_start();

    while (1) {
        EventBits_t bits = xEventGroupWaitBits(
            s_wifi_event_group,
            WIFI_CFG_CONNECTED_BIT | WIFI_CFG_SC_DONE_BIT,
            pdFALSE, // 执行完不清除位
            pdFALSE, // 等待任何一个位，而不是所有位
            portMAX_DELAY
        );

        if (bits & WIFI_CFG_CONNECTED_BIT) {
            ESP_LOGI(TAG, "WiFi Connected");
            // esp_smartconfig_stop(); // 连接成功后停止 SmartConfig
            // s_smartconfig_started = false;
            // vTaskDelete(NULL);
            vTaskDelay(pdMS_TO_TICKS(300));

            // xEventGroupClearBits(s_wifi_event_group, WIFI_CFG_CONNECTED_BIT); // 清除位，避免重复处理
            // smartconfig_stop(); // 停止 SmartConfig，防止与 web prov 冲突
            // vTaskDelete(NULL); // 任务完成自毁
        }

        if (bits & WIFI_CFG_SC_DONE_BIT) {
            ESP_LOGI(TAG, "SmartConfig Done Bit Set"); // SC_EVENT_SEND_ACK_DONE 会设置此位
            esp_smartconfig_stop();
            s_smartconfig_started = false;
            vTaskDelete(NULL);
        }
    }
}

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


void wifi_background_task(void *pv)
{
    ESP_LOGI("WIFI", "WiFi background task started");

    // 1. 强制关闭节能模式，确保配网和后续数据传输的稳定性
    esp_wifi_set_ps(WIFI_PS_NONE);
    ESP_LOGW("WIFI", "WiFi Power Save Disabled for stability");

    // 2. 【核心】启动智能配网模式
    // 注意：该函数内部通常会配置 WiFi 为 STA 模式并开启 SmartConfig 监听
    wifi_smartconfig_sta();

    // 3. 等待 WiFi 连接成功
    // 这里的 wait 函数应该在监听到 SmartConfig 成功获取信息并连接后才会返回 ESP_OK
    ESP_LOGI("WIFI", "Waiting for WiFi connection (SmartConfig/WebProv)...");
    wifi_config_wait_connected();

    // 4. 获取网络时间 (SNTP)
    // 很多云端登录需要校验时间戳，建议在登录前同步时间
    initialize_sntp_v5();

    // 5. 登录并获取服务器文件列表
    int retry_count = 3;
    bool sync_success = false;

    while (retry_count-- > 0 && !sync_success) {
        ESP_LOGI("WIFI", "Connecting to cloud... (Attempts left: %d)", retry_count + 1);

        if (http_login(&g_strResp)) {
            // 登录成功，抓取产品列表存入 g_http_resp.buffer
            if (http_get_all_products(g_strResp.token)) {
                ESP_LOGI("WIFI", "Product list synced and cached.");
                sync_success = true;
            } else {
                ESP_LOGE("WIFI", "Failed to fetch product list.");
            }
        }
        else {
            ESP_LOGE("WIFI", "Cloud login failed.");
        }

        if (!sync_success && retry_count > 0) {
            vTaskDelay(pdMS_TO_TICKS(5000)); // 失败后等 5 秒再试
        }
    }

    ESP_LOGI("WIFI", "Background task finished. Status: %s", sync_success ? "DONE" : "FAILED");
    
    // 任务完成后自毁，释放栈空间
    vTaskDelete(NULL); 
}