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
#include "lvgl_manager.h"

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
    ui_set_wifi_mode(true);
    ui_set_web_ip("192.168.4.1");
    ui_set_ip("Waiting for STA IP...");
    ui_show_wifi();
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
            ui_set_wifi_connected(false);
            ui_set_status("WiFi disconnected");

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
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        char ip_buf[20];
        snprintf(ip_buf, sizeof(ip_buf), IPSTR, IP2STR(&event->ip_info.ip));
        ui_set_ip(ip_buf);
        ui_set_wifi_connected(true);
        ui_set_status("WiFi Connected");
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
    ui_set_ssid(ssid);
    ui_set_status("Connecting WiFi...");
    ui_set_wifi_connected(false);

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
    ESP_LOGI("MEM", "Internal Free: %d KB", heap_caps_get_free_size(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL) / 1024);
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
        ui_set_wifi_mode(false);
        ui_set_ssid((char *)wifi_cfg.sta.ssid);

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
// 在 wifi_config.c 顶部添加
#include "lvgl.h"              // 必须包含，否则不认识 lv_obj_t
#include "esp_lvgl_port.h"     // 如果你用到了锁 lvgl_port_lock
static lv_obj_t *ui_init_screen;
static lv_obj_t *ui_arc_loader;
static lv_obj_t *ui_status_label;

void ui_init_screen_create(void)
{
    ui_show_page(UI_PAGE_BOOT);
    ui_set_status("Power-on initialization...");
    return;

    if (lvgl_port_lock(0)) {
        ui_init_screen = lv_obj_create(NULL);
        lv_obj_set_style_bg_color(ui_init_screen, lv_palette_main(LV_PALETTE_BLUE_GREY), 0);
        lv_obj_set_style_bg_grad_color(ui_init_screen, lv_color_hex(0x1A1A1A), 0);
        lv_obj_set_style_bg_grad_dir(ui_init_screen, LV_GRAD_DIR_VER, 0);

        // 创建中心圆环加载条
        ui_arc_loader = lv_arc_create(ui_init_screen);
        lv_obj_set_size(ui_arc_loader, 150, 150);
        lv_arc_set_rotation(ui_arc_loader, 270);
        lv_arc_set_bg_angles(ui_arc_loader, 0, 360);
        lv_obj_set_style_arc_width(ui_arc_loader, 10, LV_PART_MAIN);
        lv_obj_set_style_arc_width(ui_arc_loader, 10, LV_PART_INDICATOR);
        lv_obj_center(ui_arc_loader);
        // 让圆环动起来
        lv_arc_set_value(ui_arc_loader, 10); 

        // 状态文字
        ui_status_label = lv_label_create(ui_init_screen);
        lv_obj_set_style_text_font(ui_status_label, &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_color(ui_status_label, lv_color_white(), 0);
        lv_label_set_text(ui_status_label, "System Booting...");
        lv_obj_align(ui_status_label, LV_ALIGN_CENTER, 0, 100);

        lv_scr_load(ui_init_screen);
        lvgl_port_unlock();
    }
}

// 供后台任务调用的更新函数（线程安全）
void ui_update_init_status(const char *text, int progress, lv_color_t color)
{
    (void)progress;
    (void)color;
    ui_set_status(text);
    return;

    if (lvgl_port_lock(0)) {
        lv_label_set_text(ui_status_label, text);
        lv_arc_set_value(ui_arc_loader, progress);
        lv_obj_set_style_arc_color(ui_arc_loader, color, LV_PART_INDICATOR);
        lvgl_port_unlock();
    }
}

volatile SystemStatus_t g_sys_status;
void wifi_background_task(void *pv)
{
    g_sys_status = SYS_INIT;
    
    // 创建 LVGL 初始化界面
    ui_init_screen_create();

    // --- 步骤 1: 基础硬件 ---
    ui_update_init_status("Hardware Initializing...", 20, lv_palette_main(LV_PALETTE_BLUE));
    esp_wifi_set_ps(WIFI_PS_NONE);
    vTaskDelay(pdMS_TO_TICKS(500));

    // --- 步骤 2: WiFi 配网 ---
    g_sys_status = SYS_WIFI_WAIT;
    ui_update_init_status("Waiting for WiFi...", 40, lv_palette_main(LV_PALETTE_AMBER));
    
    wifi_smartconfig_sta();
    wifi_config_wait_connected();
    
    ui_update_init_status("WiFi Connected!", 60, lv_palette_main(LV_PALETTE_GREEN));
    vTaskDelay(pdMS_TO_TICKS(500));

    // --- 步骤 3: SNTP 时间同步 ---
    ui_update_init_status("Syncing Network Time...", 75, lv_palette_main(LV_PALETTE_CYAN));
    initialize_sntp_v5();

    // --- 步骤 4: 云端登录与数据同步 ---
    g_sys_status = SYS_SYNCING;
    int retry_count = 3;
    bool sync_success = false;

    while (retry_count-- > 0 && !sync_success) {
        char msg[32];
        snprintf(msg, sizeof(msg), "Cloud Syncing (%d)...", retry_count + 1);
        ui_update_init_status(msg, 85, lv_palette_main(LV_PALETTE_INDIGO));

        if (http_login(&g_strResp)) {
            if (http_get_all_products(g_strResp.token)) {
                sync_success = true;
            }
        }
        if (!sync_success && retry_count > 0) vTaskDelay(pdMS_TO_TICKS(2000));
    }

    // --- 步骤 5: 完成或失败 ---
    if (sync_success) {
        ui_update_init_status("SYSTEM READY", 100, lv_palette_main(LV_PALETTE_GREEN));
        vTaskDelay(pdMS_TO_TICKS(1000));
        g_sys_status = SYS_READY; 
        ui_set_status("System ready");
        ui_show_home();
        
        // 此处跳转主页逻辑
        // ui_goto_homepage(); 
    } else {
        g_sys_status = SYS_ERROR;
        ui_update_init_status("INIT FAILED!", 100, lv_palette_main(LV_PALETTE_RED));
        ui_set_status("Initialization failed");
    }
    
    vTaskDelete(NULL); 
}
