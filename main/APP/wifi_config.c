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
#include "lwip_demo.h"
#ifdef STA_AP_MODE
    #include "web_server.h"
#endif
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
#ifdef STA_AP_MODE
    web_server_start();
#endif
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
#ifdef STA_AP_MODE
            web_server_start();
#endif
            break;

        case WIFI_EVENT_STA_DISCONNECTED:
            ESP_LOGW(TAG, "WiFi disconnected");

            if(s_retry_count < WIFI_CONNECT_RETRY_MAX){
                s_retry_count++;
                ESP_LOGI(TAG, "Retrying WiFi connect %d/%d", s_retry_count, WIFI_CONNECT_RETRY_MAX);
                esp_wifi_connect();
            } else {
                ESP_LOGW(TAG, "Exceeded max retries, will start SmartConfig");
              //  xEventGroupSetBits(s_wifi_event_group, WIFI_CFG_CONNECTED_BIT); // 触发主任务启动 SmartConfig
            }
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
        ESP_LOGI("MEM", "Free heap: %d", (int)esp_get_free_heap_size());
        ESP_LOGI("MEM", "Min free heap: %d", (int)esp_get_minimum_free_heap_size());
        ESP_LOGI("MEM", "Largest free block: %d", (int)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
        mqtt_init();  // 或单独写个 mqtt_start()
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
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    /* ---------- 启动 WiFi ---------- */
    ESP_ERROR_CHECK(esp_wifi_start());

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
            char ssid_str[64] = {0};
            snprintf(ssid_str, sizeof(ssid_str), "SSID: %s", wifi_cfg.sta.ssid);
            lcd_show_string(30, 70, 200, 16, 16, ssid_str, RED);

        } else {

            ESP_LOGW(TAG, "WiFi connect failed, start SmartConfig");

            lcd_show_string(30, 70, 200, 16, 16, "SmartConfig Mode", RED);

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
    //ESP_ERROR_CHECK(esp_smartconfig_start(&cfg));
    smartconfig_start();

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
           // esp_smartconfig_stop();
            // s_smartconfig_started = false;
            // vTaskDelete(NULL);
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

esp_err_t wifi_scan_handler(httpd_req_t *req)
{
    scan_done = false;

    wifi_scan_config_t cfg = {0};
    esp_wifi_scan_start(&cfg, false); // 异步扫描
    esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_SCAN_DONE, &wifi_scan_done_cb, NULL);

    // 等待扫描完成（最多 3 秒）
    int wait_ms = 0;
    while (!scan_done && wait_ms < 3000) {
        vTaskDelay(pdMS_TO_TICKS(100));
        wait_ms += 100;
    }

    // ===== 按 RSSI 排序（信号强优先）=====
    for (int i = 0; i < g_scan_count - 1; i++) {
        for (int j = i + 1; j < g_scan_count; j++) {
            if (g_scan_results[j].rssi > g_scan_results[i].rssi) {
                scan_result_t temp = g_scan_results[i];
                g_scan_results[i] = g_scan_results[j];
                g_scan_results[j] = temp;
            }
        }
    }

    // ===== 构建 JSON =====
    cJSON *root = cJSON_CreateArray();

    for (int i = 0; i < g_scan_count; i++) {

        if (strlen((char*)g_scan_results[i].ssid) == 0)
            continue;   // 过滤空SSID

        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "ssid", (char*)g_scan_results[i].ssid);
        cJSON_AddNumberToObject(item, "rssi", g_scan_results[i].rssi);

        cJSON_AddItemToArray(root, item);
    }

    char *json_string = cJSON_PrintUnformatted(root);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_string, HTTPD_RESP_USE_STRLEN);

    cJSON_Delete(root);
    free(json_string);

    return ESP_OK;
}

esp_err_t connect_wifi_handler(httpd_req_t *req)
{
    char buf[256] = {0};
    int total_len = req->content_len;
    int cur_len = 0;
    int received = 0;

    if (total_len >= sizeof(buf)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Content too long");
        return ESP_FAIL;
    }

    // 读取完整 POST 数据
    while (cur_len < total_len) {
        received = httpd_req_recv(req, buf + cur_len, total_len - cur_len);
        if (received <= 0) {
            return ESP_FAIL;
        }
        cur_len += received;
    }

    buf[total_len] = '\0';

    ESP_LOGI(TAG, "POST DATA: %s", buf);

    // 🔥 解析 JSON
    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    const cJSON *ssid_item = cJSON_GetObjectItem(root, "ssid");
    const cJSON *pwd_item  = cJSON_GetObjectItem(root, "password");

    if (!cJSON_IsString(ssid_item) || !cJSON_IsString(pwd_item)) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid parameters");
        return ESP_FAIL;
    }

    const char *ssid = ssid_item->valuestring;
    const char *password = pwd_item->valuestring;

    ESP_LOGI(TAG, "SSID: %s", ssid);
    ESP_LOGI(TAG, "Password: %s", password);

    if (strlen(ssid) == 0) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "SSID empty");
        return ESP_FAIL;
    }

    wifi_apply_config(ssid, password);

    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"connecting\"}");

    return ESP_OK;
}


data_config_t g_data_config;

esp_err_t load_data_config(data_config_t *config)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("storage", NVS_READONLY, &handle);
    if (err != ESP_OK) return err;

    size_t required_size = sizeof(data_config_t);
    err = nvs_get_blob(handle, "data_config", config, &required_size);

    nvs_close(handle);
    return err;
}

esp_err_t save_data_config(data_config_t *config)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &handle);  // "storage" 是命名空间
    if (err != ESP_OK) return err;

    // 写入结构体
    err = nvs_set_blob(handle, "data_config", config, sizeof(data_config_t));
    if (err == ESP_OK) {
        err = nvs_commit(handle); // 提交写入
    }

    nvs_close(handle);
    return err;
}

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
    save_data_config(&g_data_config);
    cJSON_Delete(json);
    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

esp_err_t usb_files_handler(httpd_req_t *req)
{
    DIR *dir;
    struct dirent *entry;

    cJSON *root = cJSON_CreateArray();
    ESP_LOGI("USB", "usb_files_handler CALLED");
    if (!root)
    {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "JSON create failed");
        return ESP_FAIL;
    }

    dir = opendir("/disk");
    if (dir == NULL)
    {
        ESP_LOGE(TAG, "Failed to open /disk");
        cJSON_Delete(root);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "[]");
        return ESP_OK;
    }

    while ((entry = readdir(dir)) != NULL)
    {
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0 ||
            strcmp(entry->d_name, "System Volume Information") == 0)
            continue;

        cJSON_AddItemToArray(root,
                             cJSON_CreateString(entry->d_name));
    }

    closedir(dir);

    char *json_string = cJSON_PrintUnformatted(root);

    if (json_string == NULL) {
        httpd_resp_sendstr(req, "[]");
        cJSON_Delete(root);
        return ESP_OK;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_string);

    cJSON_free(json_string);
    cJSON_Delete(root);

    return ESP_OK;
}

esp_err_t get_config_handler(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();

    // 从 NVS 读取
    data_config_t cfg;
    if(load_data_config(&cfg) != ESP_OK) {
        strcpy(cfg.local_file, "未设置");
        strcpy(cfg.upload_server, "");
    }

    cJSON_AddStringToObject(root, "localFile", cfg.local_file);
    cJSON_AddStringToObject(root, "uploadServer", cfg.upload_server);

    char *out = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, out, strlen(out));

    cJSON_Delete(root);
    free(out);
    return ESP_OK;
}

esp_err_t get_wifi_info_handler(httpd_req_t *req)
{
    wifi_config_t wifi_cfg;
    memset(&wifi_cfg, 0, sizeof(wifi_cfg));

    cJSON *root = cJSON_CreateObject();

    if (esp_wifi_get_config(WIFI_IF_STA, &wifi_cfg) == ESP_OK &&
        strlen((char *)wifi_cfg.sta.ssid) > 0)
    {
        cJSON_AddStringToObject(root, "ssid", (char *)wifi_cfg.sta.ssid);
        cJSON_AddStringToObject(root, "password", (char *)wifi_cfg.sta.password);
    }
    else
    {
        cJSON_AddStringToObject(root, "ssid", "");
        cJSON_AddStringToObject(root, "password", "");
    }

    char *out = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, out, strlen(out));

    cJSON_Delete(root);
    free(out);

    return ESP_OK;
}