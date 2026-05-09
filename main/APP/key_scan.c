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
#include "ui_lvgl.h"
#include "lvgl_manager.h"
#include "key_scan.h"
#include "key.h"

// 队列句柄
esp_mqtt_client_handle_t client = NULL;

typedef enum {
    WIFI_MODE_WEB_CONFIG,   // APSTA
    WIFI_MODE_STA_ONLY      // 纯 STA
} wifi_user_mode_t;

static wifi_user_mode_t g_wifi_user_mode = WIFI_MODE_WEB_CONFIG;
void wifi_switch_mode(void)
{
    ui_set_status("System Switching...");
    ui_set_wifi_connected(false);
    ui_show_wifi();

    wifi_smartconfig_stop();
    esp_wifi_stop();

    if (g_wifi_user_mode == WIFI_MODE_WEB_CONFIG)
    {
        wifi_config_t conf;
        esp_wifi_get_config(WIFI_IF_STA, &conf);

        ui_set_status("STA ONLY MODE");
        ui_set_ssid((char *)conf.sta.ssid);
        ui_set_ip("Waiting for IP...");
        ui_set_web_ip("--");
        ui_set_wifi_mode(false);

        web_prov_stop();
        esp_wifi_set_mode(WIFI_MODE_STA);
        g_wifi_user_mode = WIFI_MODE_STA_ONLY;
    }
    else
    {
        ui_set_status("AP + STA MODE");
        ui_set_ssid("ESP32_Config");
        ui_set_ip("Waiting for STA IP...");
        ui_set_web_ip("192.168.4.1");
        ui_set_wifi_mode(true);

        esp_wifi_set_mode(WIFI_MODE_APSTA);
        web_prov_start();
        g_wifi_user_mode = WIFI_MODE_WEB_CONFIG;
    }

    esp_wifi_start();
    esp_wifi_connect();
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

        ui_set_ota("LOGIN OK");

        // ⭐ 1. 获取版本
        if (http_get_version(resp.token)) {

            ui_set_ota("GET VERSION OK");

            char buf[256] = {0};
            const char *file_name = g_download_info.file_name;

            if (file_name == NULL || file_name[0] == '\0') {
                const char *basename = strrchr(g_download_info.url, '/');
                file_name = (basename && basename[1]) ? basename + 1 : "ota_update.bin";
            }

            snprintf(buf, sizeof(buf), "%s/%s", USB_PATH, file_name);
            strcpy(g_strWriteLocalFileName, buf);

            ui_set_ota("LOADING...");
            ui_show_download();

            // ⭐ 2. 下载
            if (download_to_usb(g_download_info.url, g_strWriteLocalFileName) == ESP_OK) {

                ESP_LOGI("MAIN", "下载完成，开始 MD5 校验...");

                ui_set_ota("MD5 CHECKING...");
                lv_timer_enable(false);
                vTaskDelay(pdMS_TO_TICKS(1000));

                // ⭐ 3. MD5 校验
                if (verify_file_md5(g_strWriteLocalFileName, g_download_info.md5)) {
                vTaskDelay(pdMS_TO_TICKS(1000));
                lv_timer_enable(true);

                    ESP_LOGI("MAIN", "MD5 校验通过");

                    ui_set_ota("MD5 OK");

                    vTaskDelay(pdMS_TO_TICKS(500));

                    ui_set_ota("READY FOR OTA");

                    // 👉 真正 OTA
                    // execute_ota_update_from_usb(g_strWriteLocalFileName);

                } else {
                vTaskDelay(pdMS_TO_TICKS(500));
                lv_timer_enable(true);

                    ESP_LOGE("MAIN", "MD5 校验失败");

                    ui_set_ota("MD5 FAILED");

                    vTaskDelay(pdMS_TO_TICKS(500));

                    unlink(g_strWriteLocalFileName);
                }

            } else {
                ESP_LOGE("MAIN", "下载失败");

                ui_set_ota("DOWNLOAD FAILED");
            }
        }

    } else {
        ESP_LOGE("MAIN", "登录失败");

        ui_set_ota("LOGIN FAILED");
    }

    printf("OTA update task finished.\n");

    xOtaTaskHandle = NULL;
    vTaskDelete(NULL);
}

esp_err_t run_full_upgrade_chain(const char *token) {
    int64_t product_id = -1;
    int64_t platform_id = -1;
    char target_version_str[64] = {0}; 
    char final_download_url[1024] = {0}; // 扩容至 1024，防止 S3 签名 URL 截断
    char file_name[128] = {0};
    char md5_expect[64] = {0};
    char body_buf[256];

    // --- STEP 1: 获取产品 ID ---
    if (!http_execute_get_request(GET_PRODUCTS_URL, token)) return ESP_FAIL;
    cJSON *root = cJSON_Parse((char*)g_http_resp.buffer);
    if (!root) return ESP_FAIL;
    
    cJSON *data = cJSON_GetObjectItem(root, "data");
    cJSON *item = NULL;
    
    cJSON_ArrayForEach(item, data) {
        cJSON *code = cJSON_GetObjectItem(item, "code");
        if (cJSON_IsString(code) && strcmp(code->valuestring, PRODUCT_CODE) == 0) {
            product_id = (int64_t)cJSON_GetObjectItem(item, "id")->valuedouble;
            break;
        }
    }
    cJSON_Delete(root);
    if (product_id == -1) { ESP_LOGE("MAIN", "未找到产品: %s", PRODUCT_CODE); return ESP_FAIL; }

    // --- STEP 2: 获取平台 ID ---
    snprintf(body_buf, sizeof(body_buf), "{\"id\":%lld}", product_id);
    if (!http_execute_get_with_body(GET_PLATFORMS_URL, token, body_buf)) return ESP_FAIL;
    root = cJSON_Parse((char*)g_http_resp.buffer);
    if (!root) return ESP_FAIL;
    
    data = cJSON_GetObjectItem(root, "data");
    cJSON_ArrayForEach(item, data) {
        cJSON *code = cJSON_GetObjectItem(item, "code");
        if (cJSON_IsString(code) && strcmp(code->valuestring, PLAT_FORM_CODE) == 0) {
            platform_id = (int64_t)cJSON_GetObjectItem(item, "id")->valuedouble;
            break;
        }
    }
    cJSON_Delete(root);
    if (platform_id == -1) { ESP_LOGE("MAIN", "未找到平台: %s", PLAT_FORM_CODE); return ESP_FAIL; }

    // --- STEP 3: 寻找最新版本 ---
    snprintf(body_buf, sizeof(body_buf), "{\"id\":%lld}", platform_id);
    if (!http_execute_get_with_body(GET_VERSIONS_URL, token, body_buf)) return ESP_FAIL;
    root = cJSON_Parse((char*)g_http_resp.buffer);
    if (!root) return ESP_FAIL;
    
    data = cJSON_GetObjectItem(root, "data");
    int size = cJSON_GetArraySize(data);
    if (size > 0) {
        cJSON *latest = cJSON_GetArrayItem(data, size - 1);
        cJSON *v = cJSON_GetObjectItem(latest, "version");
        if (cJSON_IsString(v)) {
            strncpy(target_version_str, v->valuestring, sizeof(target_version_str)-1);
            ESP_LOGW("MAIN", "定位到最新版本: %s", target_version_str);
        }
    }
    cJSON_Delete(root);
    if (target_version_str[0] == '\0') { ESP_LOGE("MAIN", "版本列表为空"); return ESP_FAIL; }

    // --- STEP 4: 获取具体的下载信息 ---
    instrument_config_t instr_config = {0};
    if (load_instrument_config_from_nvs(&instr_config) != ESP_OK) return ESP_FAIL;

    cJSON *req_root = cJSON_CreateObject();
    cJSON_AddStringToObject(req_root, "platformcode", instr_config.platform_code);
    cJSON_AddStringToObject(req_root, "productcode", instr_config.product_code);
    cJSON_AddStringToObject(req_root, "version", target_version_str);
    char *json_body = cJSON_PrintUnformatted(req_root);
    
    bool ret_url = http_execute_get_with_body(DOWNLOAD_CURRENT_URL, token, json_body);
    free(json_body);
    cJSON_Delete(req_root);
    if (!ret_url) return ESP_FAIL;

    // 解析最后的下载地址信息
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

            if (cJSON_IsString(u)) strncpy(final_download_url, u->valuestring, sizeof(final_download_url)-1);
            if (cJSON_IsString(n)) strncpy(file_name, n->valuestring, sizeof(file_name)-1);
            if (cJSON_IsString(m)) strncpy(md5_expect, m->valuestring, sizeof(md5_expect)-1);
        }
    }
    
    // ⭐ 核心优化点：在进入下载函数前销毁 cJSON，彻底释放 Heap 内存
    cJSON_Delete(root); 

    // --- STEP 5: 下载与 OTA ---
    if (final_download_url[0] != '\0') {
        char full_path[256];
        if (file_name[0] == '\0') {
            snprintf(file_name, sizeof(file_name), "%s_latest.bin", target_version_str);
        }
        // 拼接路径，确保 USB 挂载点前缀正确
        snprintf(full_path, sizeof(full_path), "%s/%s", USB_PATH, file_name);
        
        ESP_LOGI("MAIN", "准备下载，目标路径: %s", full_path);
        
        if (download_to_usb(final_download_url, full_path) == ESP_OK) {
            lv_timer_enable(false);

            if (verify_file_md5(full_path, md5_expect)) {
                ESP_LOGW("MAIN", "MD5 校验成功，开始 OTA 写入...");
                ota_from_usb(full_path);
                lv_timer_enable(true);
                return ESP_OK;
            } else {
                ESP_LOGE("MAIN", "MD5 校验不匹配，删除文件");
                unlink(full_path);
                lv_timer_enable(true);
            }
        } else {
            ESP_LOGE("MAIN", "download_to_usb 执行失败");
        }
    } else {
        ESP_LOGE("MAIN", "URL 解析为空");
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
    key_init();
   // lcd_show_homepage((char *)conf.sta.ssid, "Waiting for IP...", false);
    // while (g_sys_status != SYS_READY) 
    // {
    //     //ESP_LOGW("UI", "System busy, key ignored.");
    //     vTaskDelay(pdMS_TO_TICKS(100));
    // }
    wifi_switch_mode();

    while(1)
    {
#ifdef LCD_1_47INCHL

        if (KEY1_CODE == 0) {
            key = KEY1_PRES;
            ESP_LOGE("KEY", "KEY1");
        } 
        else if (KEY3_CODE == 0) {
            ESP_LOGE("KEY", "KEY3");
            key = KEY3_PRES;
        }
        else {
            key = 0;
        }
#else
        key = xl9555_key_scan(0);
#endif
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
                    xTaskCreatePinnedToCore(
                        ota_update_task,        // 任务函数
                        "OTA_Update_Task",      // 任务名称
                        8192,                   // 栈大小：建议从 4096 增加到 8192
                                                // (OTA 和 HTTP 逻辑较深，大一点更安全)
                        NULL,                   // 传递给任务的参数
                        5,                      // 优先级：与 key_scan 同级或略低
                        &xOtaTaskHandle,        // 任务句柄
                        0                       // 【关键】指定运行在核心 1
                    );
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
