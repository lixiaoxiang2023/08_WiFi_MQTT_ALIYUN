#include "mqtt_client.h"
#include "xl9555.h"
#include "freertos/FreeRTOS.h" // Keep this one for FreeRTOS
#include "freertos/task.h"    // Keep this one for FreeRTOS
#include <string.h>
#include <stdio.h>
#include "lwip_mqtt.h"
#include "wifi_config.h"
#include "huawei_ota.h"
#include "web_server_handlers.h"



// 队列句柄
esp_mqtt_client_handle_t client = NULL;

typedef enum {
    WIFI_MODE_WEB_CONFIG,   // APSTA
    WIFI_MODE_STA_ONLY      // 纯 STA
} wifi_user_mode_t;

/**
 * @brief  系统主页 (卡片式布局 - 视觉统一版)
 * @note   已移除互斥锁，请确保仅在单任务(如 WiFi 事件或主循环)中调用
 */
void lcd_show_homepage(const char *ssid, const char *ip_str, bool is_config_mode)
{
    // 1. 底层背景：全屏刷浅灰色 (统一环境底色)
    lcd_clear(LGRAY);

    // 2. 顶层装饰：深蓝色标题栏 (0-45px)
    // 使用填充函数创建色块，增加 UI 层次感
    lcd_fill(0, 0, 320, 45, DARKBLUE);
    lcd_show_string(15, 12, 290, 24, 24, "SYSTEM MONITOR", BLACK);

    // 3. 中间层卡片：白色实心矩形 (解决背景不统一的核心)
    // 将所有文字区域统一背景为白色，这样文字渲染时边缘不会有灰色毛刺
    lcd_fill(15, 60, 305, 165, WHITE); 
    lcd_draw_rectangle(15, 60, 305, 165, GRAYBLUE); // 绘制细边框

    // --- 开始在白色卡片内写字 ---
    
    // A. 运行状态
    lcd_show_string(30, 75, 80, 16, 16, "Status:", BLACK);
    if (is_config_mode) {
        lcd_show_string(110, 75, 180, 16, 16, "STA+AP Config", GREEN);
    } else {
        lcd_show_string(110, 75, 180, 16, 16, "STA Only", GREEN);
    }

    // B. WiFi SSID (名称)
    lcd_show_string(30, 105, 80, 16, 16, "SSID  :", BLACK);
    // 判空处理：如果 SSID 为空则显示连接中
    char *display_ssid = (ssid && strlen(ssid) > 0) ? (char *)ssid : "Connecting...";
    lcd_show_string(110, 105, 180, 16, 16, display_ssid, DARKBLUE);

    // C. IP 地址
    lcd_show_string(30, 135, 80, 16, 16, "IP    :", BLACK);
    char *display_ip = (ip_str && strlen(ip_str) > 0) ? (char *)ip_str : "0.0.0.0";
    lcd_show_string(110, 135, 180, 16, 16, display_ip, BLUE);

    // 4. 底部页脚：装饰线与版本号
    lcd_draw_hline(20, 195, 280, GRAYBLUE);
    
    char ver_buf[32];
    snprintf(ver_buf, sizeof(ver_buf), "Ver: %s | HW: V1.0", FW_VERSION);
    // 使用小字体渲染版本号，放置在屏幕最下方
    lcd_show_string(30, 210, 260, 12, 12, ver_buf, GRAY);
}

static wifi_user_mode_t g_wifi_user_mode = WIFI_MODE_STA_ONLY;

void wifi_switch_mode(void)
{
    ESP_LOGI("WIFI", "Switching WiFi mode from %d...", g_wifi_user_mode);

    // --- 1. 中间过渡状态 ---
    // 先显示一个切换中的界面，避免屏幕长时间停留在旧信息上
    lcd_clear(LGRAY);
    lcd_show_string(30, 110, 260, 16, 16, "System Switching...", BRRED);

    // 2. 安全停止并销毁 MQTT
    if (client) {
        esp_mqtt_client_stop(client);
        vTaskDelay(pdMS_TO_TICKS(100)); 
        esp_mqtt_client_destroy(client);
        client = NULL;
    }

    // 3. 停止 WiFi 驱动
    esp_wifi_stop();

    // --- 4. 模式切换与 LCD 调用 ---
    if (g_wifi_user_mode == WIFI_MODE_WEB_CONFIG)
    {
        /* 目标：进入纯 STA 模式 */
        ESP_LOGI("WIFI", "Switching to [STA ONLY] Mode");
        
        // 尝试获取之前保存的配置信息（用于显示即将连接的 SSID）
        wifi_config_t conf;
        esp_wifi_get_config(WIFI_IF_STA, &conf);
        
        // 调用你要求的 LCD 主页显示
        // 刚切换时 IP 还没拿到，所以传 NULL 或 "Connecting..."
        lcd_show_homepage((char *)conf.sta.ssid, "Waiting for IP...", false);

        web_prov_stop(); 
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        g_wifi_user_mode = WIFI_MODE_STA_ONLY;
    }
    else
    {
        /* 目标：进入 AP + STA 配网模式 */
        ESP_LOGI("WIFI", "Switching to [AP + STA] Mode");
        
        // 配网模式下，SSID 通常是设备自身的热点名称 (如 ESP32-S3-Config)
        lcd_show_homepage("ESP32_Config", "192.168.4.1", true);

        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
        web_prov_start(); 
        g_wifi_user_mode = WIFI_MODE_WEB_CONFIG;
    }

    // 5. 重新启动 WiFi 并连接
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_wifi_connect(); 

    ESP_LOGI("WIFI", "WiFi mode switch finished.");
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
            snprintf(buf, sizeof(buf), "%s/%s", USB_PATH, g_download_info.file_name);
            strcpy(g_strWriteLocalFileName, buf);

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
esp_err_t run_full_upgrade_chain(const char *token) {
    int64_t product_id = -1;
    int64_t platform_id = -1;
    char target_version_str[64] = {0}; 
    char final_download_url[512] = {0};
    char file_name[128] = {0};
    char md5_expect[64] = {0};

    // --- STEP 1: 获取产品 ID (逻辑不变) ---
    if (!http_execute_get_request(GET_PRODUCTS_URL, token)) return ESP_FAIL;
    cJSON *root = cJSON_Parse((char*)g_http_resp.buffer);
    if (!root) return ESP_FAIL;
    cJSON *data = cJSON_GetObjectItem(root, "data");
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, data) {
        cJSON *code = cJSON_GetObjectItem(item, "code");
        if (code && strcmp(code->valuestring, PRODUCT_CODE) == 0) {
            product_id = (int64_t)cJSON_GetObjectItem(item, "id")->valuedouble;
            break;
        }
    }
    cJSON_Delete(root);
    if (product_id == -1) { ESP_LOGE("MAIN", "未找到产品: %s", PRODUCT_CODE); return ESP_FAIL; }

    // --- STEP 2: 获取平台 ID (逻辑不变) ---
    char body_buf[256];
    snprintf(body_buf, sizeof(body_buf), "{\"id\":%lld}", product_id);
    if (!http_execute_get_with_body(GET_PLATFORMS_URL, token, body_buf)) return ESP_FAIL;
    root = cJSON_Parse((char*)g_http_resp.buffer);
    data = cJSON_GetObjectItem(root, "data");
    cJSON_ArrayForEach(item, data) {
        cJSON *code = cJSON_GetObjectItem(item, "code");
        if (code && strcmp(code->valuestring, PLAT_FORM_CODE) == 0) {
            platform_id = (int64_t)cJSON_GetObjectItem(item, "id")->valuedouble;
            break;
        }
    }
    cJSON_Delete(root);
    if (platform_id == -1) { ESP_LOGE("MAIN", "未找到平台: %s", PLAT_FORM_CODE); return ESP_FAIL; }

    // --- STEP 3: 寻找最新版本 (修改点：取 size - 1) ---
    snprintf(body_buf, sizeof(body_buf), "{\"id\":%lld}", platform_id);
    if (!http_execute_get_with_body(GET_VERSIONS_URL, token, body_buf)) return ESP_FAIL;
    root = cJSON_Parse((char*)g_http_resp.buffer);
    data = cJSON_GetObjectItem(root, "data");
    int size = cJSON_GetArraySize(data);
    
    if (size > 0) {
        // ⭐ 修改点：取 size - 1 即为最新版本
        cJSON *latest = cJSON_GetArrayItem(data, size - 1);
        cJSON *v = cJSON_GetObjectItem(latest, "version");
        if (v) {
            strncpy(target_version_str, v->valuestring, sizeof(target_version_str)-1);
            ESP_LOGW("MAIN", "定位到最新版本号: %s", target_version_str);
        }
    }
    cJSON_Delete(root);
    if (strlen(target_version_str) == 0) { ESP_LOGE("MAIN", "版本列表为空或解析失败"); return ESP_FAIL; }

    // --- STEP 4: 请求 DOWNLOAD_URL 获取该版本的下载指令 (解析逻辑已修正) ---
    cJSON *req_root = cJSON_CreateObject();
    cJSON_AddStringToObject(req_root, "platformcode", PLAT_FORM_CODE);
    cJSON_AddStringToObject(req_root, "productcode", PRODUCT_CODE);
    cJSON_AddStringToObject(req_root, "version", target_version_str);
    char *json_body = cJSON_PrintUnformatted(req_root);
    
    ESP_LOGI("MAIN", "请求下载地址 Body: %s", json_body);
    bool ret = http_execute_get_with_body(DOWNLOAD_CURRENT_URL, token, json_body);
    free(json_body);
    cJSON_Delete(req_root);

    if (!ret) return ESP_FAIL;

    // 解析最后的下载地址信息 (修正了 files 数组层级)
    root = cJSON_Parse((char*)g_http_resp.buffer);
    if (!root) return ESP_FAIL;
    cJSON *data_obj = cJSON_GetObjectItem(root, "data");
    if (data_obj) {
        cJSON *files = cJSON_GetObjectItem(data_obj, "files");
        if (cJSON_IsArray(files) && cJSON_GetArraySize(files) > 0) {
            cJSON *f = cJSON_GetArrayItem(files, 0); 
            cJSON *u = cJSON_GetObjectItem(f, "url");
            cJSON *n = cJSON_GetObjectItem(f, "name"); 
            cJSON *m = cJSON_GetObjectItem(f, "md5");

            if (u) strncpy(final_download_url, u->valuestring, sizeof(final_download_url)-1);
            if (n) strncpy(file_name, n->valuestring, sizeof(file_name)-1);
            if (m) strncpy(md5_expect, m->valuestring, sizeof(md5_expect)-1);
        }
    }
    cJSON_Delete(root);

    // --- STEP 5: 下载与 OTA ---
    if (strlen(final_download_url) > 0) {
        char full_path[256];
        if (strlen(file_name) == 0) snprintf(file_name, sizeof(file_name), "%s_latest.bin", target_version_str);
        snprintf(full_path, sizeof(full_path), "/disk/%s", file_name);
        
        if (download_to_usb(final_download_url, full_path) == ESP_OK) {
            if (verify_file_md5(full_path, md5_expect)) {
                ESP_LOGW("MAIN", "校验通过，开始升级到最新版本...");
                ota_from_usb(full_path);
                return ESP_OK;
            } else {
                unlink(full_path);
                ESP_LOGE("MAIN", "MD5 校验失败");
            }
        }
    } else {
        ESP_LOGE("MAIN", "未能获取有效下载地址");
    }

    return ESP_FAIL;
}

void ota_daemon_task(void *pvParameter) {
    ota_msg_t msg;
    ESP_LOGI("OTA_DAEMON", "OTA 守护任务就绪...");

    while (1) {
        if (xQueueReceive(ota_queue, &msg, portMAX_DELAY) == pdPASS) 
        {
            if (run_full_upgrade_chain(g_strResp.token) == ESP_OK) {
                ESP_LOGI("MAIN", "一键全自动升级完成！设备即将重启...");
            } else {
                ESP_LOGE("MAIN", "自动升级流程在某一步骤中断。");
            }
        }
    }
}
void key_scan_task(void *arg)
{
    uint8_t key;

    wifi_config_t conf;
    esp_wifi_get_config(WIFI_IF_STA, &conf);

    lcd_show_homepage((char *)conf.sta.ssid, "Waiting for IP...", false);

    while(1)
    {
        key = xl9555_key_scan(0);
        switch (key)
        {
            case KEY0_PRES:
            {
                login_response_t login_resp = {0};

                // 1. 登录获取 Token
                if (!http_login(&login_resp)) {
                    ESP_LOGE("MAIN", "步骤1失败: 登录鉴权未通过");
                    break; 
                }

                // 2. 获取产品列表 (Products)
                if (http_get_all_products(login_resp.token)) {
                    ESP_LOGI("MAIN", "步骤2成功: 已获取产品列表");
                    // 这里建议立即处理 g_http_resp.buffer，比如解析出产品ID或存入其他变量
                } else {
                    ESP_LOGE("MAIN", "步骤2失败: 产品列表拉取异常");
                }
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
