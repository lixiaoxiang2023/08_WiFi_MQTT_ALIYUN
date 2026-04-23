
/*
 * obs_http_single.c
 * 量产级 ESP32 + 华为 OBS HTTP 客户端
 * 模式：MQTT下发临时URL，不在设备侧计算签名
 * 支持：下载 / 上传 / 桶文件列表查询
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_system.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "lcd.h"
#include "obs_http.h"
#include "lvgl_manager.h"
#include "mbedtls/md5.h" // 必须包含这个头文件
#include "web_server_handlers.h" // 确保包含新的 NVS 配置处理器头文件
#include <sys/stat.h> // 必须引入此头文件获取文件信息
#include "lvgl_manager.h"

#define OBS_TAG "OBS_HTTP"
#define OBS_MAX_RETRY 3
#define OBS_RX_BUF   (4* 1024)
#define OBS_TX_BUF  (4 * 1024)
#define MAX_HTTP_OUTPUT_BUFFER 8096


http_response_t g_http_resp;

extern const uint8_t _binary_huaweicloud_iot_root_ca_list_pem_start[];
extern const uint8_t _binary_huaweicloud_iot_root_ca_list_pem_end[];

typedef struct {
    FILE *fp;
    size_t content_length;
    size_t received;
    int retry_count;
    bool finished;
    bool success;
} obs_http_state_t;


static obs_http_state_t g_state;

/* ===================== 回调 ===================== */
static esp_err_t obs_http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {

    case HTTP_EVENT_ERROR:
        ESP_LOGE(OBS_TAG, "HTTP_EVENT_ERROR");
        g_state.success = false;
        break;

    case HTTP_EVENT_ON_CONNECTED:
        ESP_LOGI(OBS_TAG, "Connected");
        break;

    case HTTP_EVENT_HEADERS_SENT:
        ESP_LOGI(OBS_TAG, "Headers sent");
        break;

    case HTTP_EVENT_ON_HEADER:
        // if (strcasecmp(evt->header_key, "Content-Length") == 0) {
        //     g_state.content_length = atoi(evt->header_value);
        //     ESP_LOGI(OBS_TAG, "Content-Length = %d", g_state.content_length);
        // }              

        if (evt->header_key && evt->header_value) {
            ESP_LOGI(OBS_TAG, "HDR: %s = %s",
                    evt->header_key,
                    evt->header_value);

            if (strcasecmp(evt->header_key, "x-obs-request-id") == 0) {
                ESP_LOGI(OBS_TAG, "OBS CONFIRM ID: %s", evt->header_value);
            }
        }
        break;

    case HTTP_EVENT_ON_DATA:
        if (evt->data_len > 0 && g_state.fp) {
           fwrite(evt->data, 1, evt->data_len, g_state.fp);
           g_state.received += evt->data_len;
        }
        break;

    case HTTP_EVENT_ON_FINISH:
        ESP_LOGI(OBS_TAG, "Transfer finished");
        g_state.finished = true;
        if (g_state.fp) {
            fclose(g_state.fp);
            g_state.fp = NULL;
        }
        if (g_state.content_length == 0 ||
            g_state.received == g_state.content_length) {
            g_state.success = true;
        } else {
            ESP_LOGE(OBS_TAG, "Size mismatch %d/%d",
                     g_state.received, g_state.content_length);
            g_state.success = false;
        }
        break;

    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGW(OBS_TAG, "Disconnected");
        break;

    default:
        break;
    }

    return ESP_OK;
}

/* ===================== 回调 ===================== */
static esp_err_t urit_http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {

    case HTTP_EVENT_ERROR:
        ESP_LOGE("HTTP", "ERROR");
        break;

    case HTTP_EVENT_ON_CONNECTED:
        ESP_LOGI("HTTP", "Connected");
        break;

    case HTTP_EVENT_HEADERS_SENT:
        ESP_LOGI("HTTP", "Headers sent");
        break;

    case HTTP_EVENT_ON_HEADER:
        // 👉 只保留关键调试（可选）
         ESP_LOGI("HTTP", "HDR: %s = %s", evt->header_key, evt->header_value);
        break;

    case HTTP_EVENT_ON_DATA:
        if (evt->data && evt->data_len > 0) {

            ESP_LOGI("HTTP", "recv len = %d", evt->data_len);

            if (g_http_resp.buffer &&
                g_http_resp.len + evt->data_len < MAX_HTTP_OUTPUT_BUFFER) {

                memcpy(g_http_resp.buffer + g_http_resp.len,
                       evt->data,
                       evt->data_len);

                g_http_resp.len += evt->data_len;
            } else {
                ESP_LOGE("HTTP", "buffer overflow");
            }
        }
        break;

    case HTTP_EVENT_ON_FINISH:
        ESP_LOGI("HTTP", "Finish");
        // 打印接收到的完整JSON响应
        if (g_http_resp.buffer && g_http_resp.len > 0) {
            ESP_LOGI("HTTP", "响应成功:");
            ESP_LOGI("HTTP", "%.*s", g_http_resp.len, g_http_resp.buffer);
        }
        break;

    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGW("HTTP", "Disconnected");
        break;

    default:
        break;
    }

    return ESP_OK;
}
/* ===================== 内部工具 ===================== */
static void obs_state_reset(void)
{
    memset(&g_state, 0, sizeof(g_state));
}
/* download progress is pushed through lvgl_manager.h */


/* ===================== 下载 ===================== */

esp_err_t download_to_usb(const char *url, const char *filename) {
    if (url == NULL || filename == NULL) return ESP_ERR_INVALID_ARG;
   vTaskDelay(pdMS_TO_TICKS(1000));

   lv_timer_enable(false);
   vTaskDelay(pdMS_TO_TICKS(100));

    // --- 1. 获取本地已下载的大小 ---
    long local_file_size = 0;
    struct stat st;
    if (stat(filename, &st) == 0) {
        local_file_size = st.st_size;
        ESP_LOGI("DW", "Detect local file: %ld bytes", local_file_size);
    }
    lv_timer_enable(true);

    // --- 2. UI 初始化 ---
    const char *short_name = strrchr(filename, '/');
    short_name = (short_name == NULL) ? filename : (short_name + 1);

    ESP_LOGI("DW", "filename:%s", short_name);
    ui_set_download_info(short_name, (uint32_t)((local_file_size > 0) ? local_file_size : 0));
    ui_show_download();
    ui_push_download(0, 0.0f, 0, 0,0,0); // 初始化UI显示
    // --- 3. 打开文件 ---
    lv_timer_enable(false);

    FILE *f = fopen(filename, "ab");
    if (f == NULL) {
        ESP_LOGE("DW", "无法打开文件: %s, 原因: %s", filename, strerror(errno));
        return ESP_FAIL;
    }

    // 时间记录
    struct timespec start_time, last_speed_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);
    last_speed_time = start_time;

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 40000,
        .buffer_size = 16384,     
        .buffer_size_tx = 4096,
        .skip_cert_common_name_check = true,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) { fclose(f); return ESP_FAIL; }

    if (local_file_size > 0) {
        char range_header[64];
        snprintf(range_header, sizeof(range_header), "bytes=%ld-", local_file_size);
        esp_http_client_set_header(client, "Range", range_header);
    }

    if (esp_http_client_open(client, 0) != ESP_OK) {
        esp_http_client_cleanup(client); fclose(f); return ESP_FAIL;
    }

    int64_t remaining_length = esp_http_client_fetch_headers(client);
    int status_code = esp_http_client_get_status_code(client);    
    esp_err_t err = ESP_OK;

    #define DL_BUFFER_SIZE (32 * 1024)
    #define PROGRESS_UPDATE_INTERVAL (256 * 1024) 

    // --- 5. 核心逻辑处理 ---
    int64_t total_content_length = 0;
    bool skip_download = false;

    if (status_code == 206) {
        total_content_length = remaining_length + local_file_size;
    } else if (status_code == 416) {
        ESP_LOGI("DW", "HTTP 416: File already complete.");
        total_content_length = local_file_size;
        skip_download = true;
    } else if (status_code == 200) {
        if (local_file_size > 0) {
            ESP_LOGW("DW", "Server returned 200, restarting...");
            fclose(f);
            f = fopen(filename, "wb");
            local_file_size = 0;
        }
        total_content_length = remaining_length;
    } else {
        ESP_LOGE("DW", "HTTP Error: %d", status_code);
        skip_download = true; 
    }

    ui_set_download_info(short_name, (uint32_t)((total_content_length > 0) ? total_content_length : local_file_size));

    // --- 6. 数据接收 ---
    int total_read_len = local_file_size;
    if (!skip_download) {
        int last_percentage = -1;
        uint32_t last_speed_bytes = local_file_size;
        
        char *buffer = malloc(DL_BUFFER_SIZE);
        if (buffer) {
            while (true) {
                int read_len = esp_http_client_read(client, buffer, DL_BUFFER_SIZE);
                if (read_len > 0) {
                    lv_timer_enable(false);
                    fwrite(buffer, 1, read_len, f);
                    total_read_len += read_len;

                    if (total_read_len >= last_speed_bytes + (PROGRESS_UPDATE_INTERVAL) || total_read_len >= total_content_length) {
                        struct timespec now_time;
                        clock_gettime(CLOCK_MONOTONIC, &now_time);
                        double diff_s = (double)(now_time.tv_sec - last_speed_time.tv_sec) + 
                                       (double)(now_time.tv_nsec - last_speed_time.tv_nsec) / 1e9;
                        double instant_speed = (diff_s > 0) ? ((total_read_len - last_speed_bytes) / 1024.0) / diff_s : 0;
                        
                        // ⭐ 修复：使用 total_content_length 计算全量百分比
                        int percentage = (total_content_length > 0) ? (int)((total_read_len * 100LL) / total_content_length) : 0;
                        
                        // 计算 ETA
                        int eta_min = 0, eta_sec = 0;
                        if (instant_speed > 0 && total_content_length > total_read_len) {
                            double remaining_kb = (double)(total_content_length - total_read_len) / 1024.0;
                            int remaining_s = (int)(remaining_kb / instant_speed);
                            eta_min = remaining_s / 60;
                            eta_sec = remaining_s % 60;
                        }

                        ESP_LOGI("DW", "[%d%%] Received: %d KB | Speed: %.2f KB/s | ETA: %02d:%02d", 
                                 percentage, total_read_len/1024, instant_speed, eta_min, eta_sec);

                        if (percentage != last_percentage) {
                            last_percentage = percentage;
                           // lv_timer_enable(true);
                          //  ui_push_download(percentage, instant_speed, eta_min, eta_sec,0,0); // 更新UI显示
                          //  lv_timer_enable(false);
                        }
                        last_speed_bytes = total_read_len;
                        last_speed_time = now_time;
                    }
                } else {
                    err = (read_len == 0 && esp_http_client_is_complete_data_received(client)) ? ESP_OK : ESP_FAIL;
                    break;
                }
            }
            free(buffer);
        }
    }

    // --- 7. 清理 ---
    bool is_done = (status_code == 416) || (err == ESP_OK && esp_http_client_is_complete_data_received(client));
    
    fflush(f);
    fsync(fileno(f)); 
    fclose(f);
    esp_http_client_cleanup(client);

    lv_timer_enable(true);
    lv_tick_inc(0);
    if (is_done) {
        return ESP_OK;
    } else {
        return ESP_FAIL;
    }
}
/* ===================== 下载 ===================== */
esp_err_t obs_http_download(const char *url, const char *local_path)
{
    if (!url || !local_path) return ESP_ERR_INVALID_ARG;

    obs_state_reset();

    g_state.fp = fopen(local_path, "wb");
    if (!g_state.fp) {
        ESP_LOGE(OBS_TAG, "Failed to open file: %s", local_path);
        return ESP_FAIL;
    }

    esp_http_client_config_t config = {
        .url = url,
        .event_handler = obs_http_event_handler,
        .cert_pem = (const char *)_binary_huaweicloud_iot_root_ca_list_pem_start,
        .timeout_ms = 15000,
        .buffer_size = OBS_RX_BUF,
        .buffer_size_tx = OBS_TX_BUF
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return ESP_FAIL;

    for (g_state.retry_count = 0;
         g_state.retry_count < OBS_MAX_RETRY;
         g_state.retry_count++) {

        ESP_LOGI(OBS_TAG, "Download try %d", g_state.retry_count + 1);
        /* =================== 这里加 Header =================== */

        // 常规头
        esp_http_client_set_header(client, "Content-Type", "text/plain");

        esp_err_t err = esp_http_client_perform(client);
        if (err == ESP_OK && g_state.success) {
            ESP_LOGI(OBS_TAG, "Download OK");
            esp_http_client_cleanup(client);
            return ESP_OK;
        }

        ESP_LOGW(OBS_TAG, "Retrying... err %d",err);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }

    esp_http_client_cleanup(client);
    return ESP_FAIL;
}

/* ===================== 上传 ===================== */
esp_err_t obs_http_upload(const char *url, const char *local_path)
{
    if (!url || !local_path) {
        ESP_LOGE(OBS_TAG, "Invalid args");
        return ESP_ERR_INVALID_ARG;
    }

    FILE *fp = fopen(local_path, "rb");
    if (!fp) {
        ESP_LOGE(OBS_TAG, "Open upload file failed: %s", local_path);
        return ESP_FAIL;
    }

    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    rewind(fp);

    ESP_LOGI(OBS_TAG, "Uploading %ld bytes to OBS", file_size);

    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 30000,

        // 关键点：OBS + HTTPS + 长URL 必须大buffer
        .buffer_size = 4096,
        .buffer_size_tx = 4096,

        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .event_handler = obs_http_event_handler,
        .cert_pem = (const char *)_binary_huaweicloud_iot_root_ca_list_pem_start,
        .keep_alive_enable = false,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(OBS_TAG, "HTTP client init failed");
        fclose(fp);
        return ESP_FAIL;
    }

    esp_http_client_set_method(client, HTTP_METHOD_PUT);
        // 常规头
    esp_http_client_set_header(client, "Content-Type", "text/plain");

    esp_err_t err = esp_http_client_open(client, file_size);
    if (err != ESP_OK) {
        ESP_LOGE(OBS_TAG, "Client open failed: %s", esp_err_to_name(err));
        goto cleanup;
    }

    uint8_t buf[1024];
    int total_sent = 0;

    while (1) {
        int r = fread(buf, 1, sizeof(buf), fp);
        if (r <= 0) break;

        int w = esp_http_client_write(client, (const char *)buf, r);
        if (w <= 0) {
            ESP_LOGE(OBS_TAG, "Write failed");
            goto cleanup;
        }

        total_sent += w;
        ESP_LOGI(OBS_TAG, "Progress: %d / %ld", total_sent, file_size);

    }

    ESP_LOGI(OBS_TAG, "Upload finished %d/%ld", total_sent, file_size);

    esp_http_client_fetch_headers(client);

    int status = esp_http_client_get_status_code(client);
    ESP_LOGI(OBS_TAG, "HTTP STATUS = %d", status);

    if (status >= 400) {
        ESP_LOGE(OBS_TAG, "OBS ERROR BODY:");
        char errbuf[256];
        int r;
        while ((r = esp_http_client_read(client, errbuf, sizeof(errbuf) - 1)) > 0) {
            errbuf[r] = 0;
            ESP_LOGE(OBS_TAG, "%s", errbuf);
        }
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    fclose(fp);

    if (status != 200 && status != 201) {
        ESP_LOGE(OBS_TAG, "OBS rejected upload");
        return ESP_FAIL;
    }

    if (total_sent != file_size) {
        ESP_LOGE(OBS_TAG, "Size mismatch %d/%ld", total_sent, file_size);
        return ESP_FAIL;
    }

    ESP_LOGI(OBS_TAG, "OBS upload SUCCESS");
    return ESP_OK;

cleanup:
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    fclose(fp);
    return ESP_FAIL;
}

bool http_post_json(const char *url, cJSON *json_body, http_response_t *resp, const char *token)
{
    if (!url || !json_body || !resp) return false;

    /* 初始化响应结构体 */
    memset(resp, 0, sizeof(http_response_t));
    resp->buffer = malloc(MAX_HTTP_OUTPUT_BUFFER);
    if (!resp->buffer) return false;

    /* 初始化全局响应结构体，用于回调 */
    memset(&g_http_resp, 0, sizeof(g_http_resp));
    g_http_resp.buffer = resp->buffer;
    g_http_resp.len = 0;

    /* ESP HTTP 配置 */
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 30000, // 延长超时，避免慢服务器导致 EAGAIN
        .event_handler = urit_http_event_handler,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        free(resp->buffer);
        return false;
    }

    /* 构造 POST 数据 */
    char *post_data = cJSON_PrintUnformatted(json_body);
    if (!post_data) {
        esp_http_client_cleanup(client);
        free(resp->buffer);
        return false;
    }

    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_post_field(client, post_data, strlen(post_data));
    esp_http_client_set_header(client, "Content-Type", "application/json");


    /* 设置 Content-Length，避免部分服务器解析失败 */
    char len_str[16];
    sprintf(len_str, "%d", (int)strlen(post_data));
    esp_http_client_set_header(client, "Content-Length", len_str);

    ESP_LOGI("HTTP", "POST: %s", post_data);

    /* 发送请求 */
    esp_err_t err = esp_http_client_perform(client);
    free(post_data);

    if (err != ESP_OK) {
        ESP_LOGE("HTTP", "请求失败: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        free(resp->buffer);
        return false;
    }

    int status = esp_http_client_get_status_code(client);
    ESP_LOGI("HTTP", "Status = %d", status);

    /* 回调已经累加到 g_http_resp.buffer */
    resp->len = g_http_resp.len;
    resp->buffer[resp->len] = '\0';

    if (resp->len == 0) {
        ESP_LOGW("HTTP", "服务器返回空数据");
    } else {
        ESP_LOGI("HTTP", "响应: %s", resp->buffer);
    }

    esp_http_client_cleanup(client);
    return true;
}

bool http_login(login_response_t *resp)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "username", LOGIC_NAME);
    cJSON_AddStringToObject(root, "password", LOGIC_PASSWORD);

    http_response_t http_resp;
    bool ok = http_post_json(LOGIC_URL, root, &http_resp, NULL);
    cJSON_Delete(root);

    if (!ok) return false;

    return parse_login_response(http_resp.buffer, resp);
}

/**
 * @brief 获取所有产品列表
 * @param token 登录获取的 Bearer Token
 * @return true 成功 / false 失败
 */
bool http_get_all_products(const char *token)
{
    if (token == NULL) return false;

    // 1. 清理并准备响应缓存
    memset(&g_http_resp, 0, sizeof(g_http_resp));
    if (g_http_resp.buffer == NULL) {
        g_http_resp.buffer = malloc(MAX_HTTP_OUTPUT_BUFFER);
    }
    if (!g_http_resp.buffer) return false;
    memset(g_http_resp.buffer, 0, MAX_HTTP_OUTPUT_BUFFER);

    // 2. 配置 HTTP 客户端
    esp_http_client_config_t config = {
        .url = GET_PRODUCTS_URL,
        .method = HTTP_METHOD_GET,     // 
        .timeout_ms = 30000,
        .event_handler = urit_http_event_handler,
        .user_data = &g_http_resp,
        .buffer_size = 2048,           // 预防 Header 过长
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        return false;
    }

    // 3. 设置鉴权 Header
    char auth_header[600];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", token);
    esp_http_client_set_header(client, "Authorization", auth_header);

    ESP_LOGI("HTTP", ">>>>>>>>> [DEBUG] GET PRODUCTS >>>>>>>>>");
    ESP_LOGI("HTTP", "URL: %s", config.url);

    // 4. 执行请求
    esp_err_t err = esp_http_client_perform(client);
    
    bool success = false;
    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        if (status == 200 && g_http_resp.len > 0) {
            // 确保字符串正常结束
            g_http_resp.buffer[g_http_resp.len < MAX_HTTP_OUTPUT_BUFFER ? g_http_resp.len : MAX_HTTP_OUTPUT_BUFFER - 1] = '\0';
            
            ESP_LOGI("HTTP", "<<<<<<<<< [RECV] PRODUCTS LIST <<<<<<<<<");
            ESP_LOGI("HTTP", "JSON: %s", (char*)g_http_resp.buffer);
            success = true;
        } else {
            ESP_LOGE("HTTP", "请求失败，状态码: %d", status);
        }
    } else {
        ESP_LOGE("HTTP", "HTTP 执行错误: %s", esp_err_to_name(err));
    }

    // 5. 清理资源
    esp_http_client_cleanup(client);
    return success;
}

/**
 * @brief 通用 POST 请求执行函数
 * @param url 请求地址
 * @param token 鉴权 Token
 * @param post_data JSON 字符串内容
 * @return true 成功 / false 失败
 */
/**
 * @brief 执行带 Body 的 GET 请求
 * @param url 请求地址
 * @param token 鉴权 Token
 * @param json_body JSON 字符串内容
 */
bool http_execute_get_with_body(const char *url, const char *token, const char *json_body) {
    if (url == NULL || token == NULL || json_body == NULL) return false;

    // 1. 缓存准备
    if (g_http_resp.buffer == NULL) {
        g_http_resp.buffer = malloc(MAX_HTTP_OUTPUT_BUFFER);
    }
    if (!g_http_resp.buffer) return false;
    memset(g_http_resp.buffer, 0, MAX_HTTP_OUTPUT_BUFFER);
    g_http_resp.len = 0;

    // 2. 配置：该接口实际上发送带 JSON Body 的请求，使用 POST 更符合服务端实现
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .event_handler = urit_http_event_handler,
        .user_data = &g_http_resp,
        .buffer_size = 2048,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return false;

    // 3. 设置 Header
    char auth_header[600];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", token);
    esp_http_client_set_header(client, "Authorization", auth_header);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    
    char len_str[16];
    sprintf(len_str, "%d", (int)strlen(json_body));
    esp_http_client_set_header(client, "Content-Length", len_str);

    // 4. POST 请求中设置 Body 数据
    esp_http_client_set_post_field(client, json_body, strlen(json_body));

    ESP_LOGI("HTTP", "发送 POST (带 Body) -> %s", url);
    ESP_LOGI("HTTP", "Body 内容: %s", json_body);

    // 5. 执行
    esp_err_t err = esp_http_client_perform(client);
    bool success = false;

    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        if (status == 200) {
            uint32_t safe_len = (g_http_resp.len < MAX_HTTP_OUTPUT_BUFFER) ? g_http_resp.len : (MAX_HTTP_OUTPUT_BUFFER - 1);
            g_http_resp.buffer[safe_len] = '\0';
            
            ESP_LOGI("HTTP", "响应成功:");
            // 还是用我之前写给你的分段打印
            char *ptr = (char*)g_http_resp.buffer;
            int rem = strlen(ptr);
            int off = 0;
            while (rem > 0) {
                int show = (rem > 200) ? 200 : rem;
                ESP_LOGI("HTTP", "%.*s", show, ptr + off);
                off += show; rem -= show;
            }
            success = true;
        } else {
            ESP_LOGE("HTTP", "失败, 状态码: %d", status);
        }
    }

    esp_http_client_cleanup(client);
    return success;
}
/**
 * @brief 内部通用 GET 请求函数
 */
bool http_execute_get_request(const char *url, const char *token) {
    if (url == NULL || token == NULL) return false;

    // 1. 准备/清理缓存
    if (g_http_resp.buffer == NULL) {
        g_http_resp.buffer = malloc(MAX_HTTP_OUTPUT_BUFFER);
    }
    if (!g_http_resp.buffer) return false;
    
    memset(g_http_resp.buffer, 0, MAX_HTTP_OUTPUT_BUFFER);
    g_http_resp.len = 0; 

    // 2. 配置 HTTP 客户端
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 30000, 
        .event_handler = urit_http_event_handler,
        .user_data = &g_http_resp,
        .buffer_size = 2048,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return false;

    // 3. 设置鉴权 Header
    char auth_header[600];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", token);
    esp_http_client_set_header(client, "Authorization", auth_header);

    ESP_LOGI("HTTP", "请求 URL: %s", url);

    // 4. 执行请求
    esp_err_t err = esp_http_client_perform(client);
    bool success = false;

    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        if (status == 200 && g_http_resp.len > 0) {
            // 确保字符串正常结束
            uint32_t safe_len = (g_http_resp.len < MAX_HTTP_OUTPUT_BUFFER) ? g_http_resp.len : (MAX_HTTP_OUTPUT_BUFFER - 1);
            g_http_resp.buffer[safe_len] = '\0';

            // --- 重点：使用 ESP_LOGI 打印数据 ---
            ESP_LOGI("HTTP", "接收成功! 状态码: %d, 字节数: %d", status, g_http_resp.len);
            
            // 如果数据可能很长，分段打印（每段 200 字节）
            char *data_ptr = (char*)g_http_resp.buffer;
            int remaining = strlen(data_ptr);
            int offset = 0;
            const int chunk_size = 200; // 安全的分段长度

            ESP_LOGI("HTTP", "--- [JSON 内容开始] ---");
            while (remaining > 0) {
                int show = (remaining > chunk_size) ? chunk_size : remaining;
                // 使用 %.长度s 来打印指定长度的子串
                ESP_LOGI("HTTP", "%.*s", show, data_ptr + offset);
                offset += show;
                remaining -= show;
            }
            ESP_LOGI("HTTP", "--- [JSON 内容结束] ---");
            
            success = true;
        } else {
            ESP_LOGE("HTTP", "请求失败! 状态码: %d", status);
        }
    } else {
        ESP_LOGE("HTTP", "传输错误: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    return success;
}


/**
 * @brief 获取产品平台列表 (优化版)
 * @param token 鉴权令牌
 * @param product_id 产品ID
 */
bool http_get_product_platforms(const char *token, int64_t product_id) {
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) return false;

    // 正确添加整数类型字段
    cJSON_AddNumberToObject(root, "id", (double)product_id);

    char *post_data = cJSON_PrintUnformatted(root);
    
    // 注意：这里的 http_execute_get_request 需要支持传入 post_data 
    // 如果你现在的函数只支持 GET 且不带 Body，则此方法不适用
    ESP_LOGI("HTTP", "发送请求体: %s", post_data);

    // 模拟执行 (假设你扩展了 http_execute 函数以支持 Body)
     bool ret = http_execute_get_with_body(GET_PLATFORMS_URL, token, post_data);
    // bool ret = http_execute_get_request(GET_PLATFORMS_URL, token); 

    // 释放内存
    cJSON_Delete(root);
    free(post_data);
    
    return ret;
}

/**
 * @brief 3. 获取平台所有版本列表
 */
bool http_get_platform_versions(const char *token, int64_t product_id) {
    ESP_LOGI("HTTP", ">>>>>> [STEP 3] GET PLATFORM VERSIONS >>>>>>");
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) return false;

    // 正确添加整数类型字段
    cJSON_AddNumberToObject(root, "id", (double)product_id);

    char *post_data = cJSON_PrintUnformatted(root);
    
    ESP_LOGI("HTTP", "发送请求体: %s", post_data);

    bool ret = http_execute_get_with_body(GET_VERSIONS_URL,  token, post_data);
    if (ret) {
        ESP_LOGD("HTTP", "VERSIONS JSON: %s", (char*)g_http_resp.buffer);
    }
    cJSON_Delete(root);
    free(post_data);
    return ret;
}
// 全变量定义
ota_info_t g_download_info = {0};

// 定义通用 HTTP 客户端的日志标签
static const char *GENERIC_HTTP_TAG = "GENERIC_HTTP";

/**
 * @brief 通用 HTTP 事件处理器。
 *        此函数会根据事件类型将接收到的数据存储到 user_data 指向的
 *        generic_http_response_t 结构体中。
 */
esp_err_t generic_http_event_handler(esp_http_client_event_t *evt)
{
    generic_http_response_t *response = (generic_http_response_t *)evt->user_data;

    // 检查响应结构体和缓冲区是否已正确初始化
    if (!response || !response->buffer) {
        ESP_LOGE(GENERIC_HTTP_TAG, "Generic HTTP response struct or buffer not initialized!");
        return ESP_FAIL;
    }

    switch (evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGD(GENERIC_HTTP_TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(GENERIC_HTTP_TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(GENERIC_HTTP_TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(GENERIC_HTTP_TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(GENERIC_HTTP_TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            // 将接收到的数据追加到响应缓冲区
            if (response->len + evt->data_len > MAX_HTTP_OUTPUT_BUFFER - 1) { // -1 为字符串终止符留空间
                int copy_len = MAX_HTTP_OUTPUT_BUFFER - 1 - response->len;
                if (copy_len > 0) {
                    memcpy(response->buffer + response->len, evt->data, copy_len);
                    response->len += copy_len;
                    ESP_LOGW(GENERIC_HTTP_TAG, "Truncating HTTP response, buffer full. Copied %d bytes.", copy_len);
                } else {
                    ESP_LOGW(GENERIC_HTTP_TAG, "HTTP response buffer already full, discarding remaining data.");
                }
            } else {
                memcpy(response->buffer + response->len, evt->data, evt->data_len);
                response->len += evt->data_len;
            }
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(GENERIC_HTTP_TAG, "HTTP_EVENT_ON_FINISH");
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGD(GENERIC_HTTP_TAG, "HTTP_EVENT_DISCONNECTED");
            break;
        case HTTP_EVENT_REDIRECT:
            ESP_LOGD(GENERIC_HTTP_TAG, "HTTP_EVENT_REDIRECT");
            break;
    }
    return ESP_OK;
}

bool http_send_request(
    const char *url,
    esp_http_client_method_t method,
    const char *auth_token,
    const char *content_type,
    const char *post_data,
    generic_http_response_t *response_out)
{
    if (url == NULL || response_out == NULL) {
        ESP_LOGE(GENERIC_HTTP_TAG, "Invalid arguments: URL or response_out is NULL.");
        return false;
    }

    // 1. 初始化响应结构体并分配缓冲区
    memset(response_out, 0, sizeof(generic_http_response_t));
    response_out->buffer = (char *)malloc(MAX_HTTP_OUTPUT_BUFFER);
    if (!response_out->buffer) {
        ESP_LOGE(GENERIC_HTTP_TAG, "Failed to allocate memory for HTTP response buffer.");
        return false;
    }
    memset(response_out->buffer, 0, MAX_HTTP_OUTPUT_BUFFER);

    // 2. 配置 HTTP 客户端
    esp_http_client_config_t config = {
        .url = url,
        .method = method,
        .timeout_ms = 30000, // 默认 8 秒超时
        .event_handler = generic_http_event_handler, // 使用通用事件处理器
        .user_data = response_out, // 将响应结构体作为用户数据传递给事件处理器
        .buffer_size = 2048, // ESP-IDF HTTP 客户端内部读缓冲区大小
        .disable_auto_redirect = true, // 默认禁用自动重定向，可在需要时开启
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(GENERIC_HTTP_TAG, "Failed to initialize HTTP client.");
        free(response_out->buffer);
        response_out->buffer = NULL;
        return false;
    }

    // 3. 设置请求头
    if (content_type) {
        esp_http_client_set_header(client, "Content-Type", content_type);
    } else if (method == HTTP_METHOD_POST || method == HTTP_METHOD_PUT) {
        // 对于 POST/PUT 请求，如果没有指定 Content-Type，则默认为 application/json
        esp_http_client_set_header(client, "Content-Type", "application/json");
    }

    if (auth_token) {
        char auth_header[600]; // 足够存储 "Bearer " + token
        snprintf(auth_header, sizeof(auth_header), "Bearer %s", auth_token);
        esp_http_client_set_header(client, "Authorization", auth_header);
        // 为了安全，日志中不打印完整的 token
        ESP_LOGD(GENERIC_HTTP_TAG, "Header: Authorization: Bearer <token_set>");
    }

    ESP_LOGI(GENERIC_HTTP_TAG, ">>>>>>>>> [DEBUG] HTTP SEND REQUEST >>>>>>>>>");
    ESP_LOGI(GENERIC_HTTP_TAG, "URL: %s", url);
    ESP_LOGI(GENERIC_HTTP_TAG, "Method: %s", (method == HTTP_METHOD_POST) ? "POST" :
                                             (method == HTTP_METHOD_GET) ? "GET" :
                                             (method == HTTP_METHOD_PUT) ? "PUT" : "UNKNOWN");


    // 4. 设置 POST/PUT 请求体
    if ((method == HTTP_METHOD_POST || method == HTTP_METHOD_PUT) && post_data) {
        ESP_LOGI(GENERIC_HTTP_TAG, "Body: %s", post_data);
        esp_http_client_set_post_field(client, post_data, strlen(post_data));
    }
    ESP_LOGI(GENERIC_HTTP_TAG, ">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>");

    // 5. 执行请求
    esp_err_t err = esp_http_client_perform(client);

    bool success = false;
    if (err == ESP_OK) {
        response_out->status_code = esp_http_client_get_status_code(client);

        // 无论状态码如何，都确保响应体以 NULL 终止
        if (response_out->len < MAX_HTTP_OUTPUT_BUFFER) {
            response_out->buffer[response_out->len] = '\0';
        } else {
            response_out->buffer[MAX_HTTP_OUTPUT_BUFFER - 1] = '\0';
        }

        ESP_LOGI(GENERIC_HTTP_TAG, "<<<<<<<<< [DEBUG] HTTP RECV RESPONSE <<<<<<<<<");
        ESP_LOGI(GENERIC_HTTP_TAG, "Status: %d", response_out->status_code);
        ESP_LOGI(GENERIC_HTTP_TAG, "Body: %s", response_out->buffer);
        ESP_LOGI(GENERIC_HTTP_TAG, "<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<");

        // 这里的 success 仅表示 HTTP 传输本身成功，业务逻辑成功与否由 status_code 和 response->buffer 内容决定
        success = true; 
    } else {
        ESP_LOGE(GENERIC_HTTP_TAG, "HTTP request failed: %s", esp_err_to_name(err));
        // 如果失败，确保 buffer 仍以 null 终止（如果接收到任何数据）
        if (response_out->len > 0 && response_out->len < MAX_HTTP_OUTPUT_BUFFER) {
            response_out->buffer[response_out->len] = '\0';
        } else if (response_out->len >= MAX_HTTP_OUTPUT_BUFFER) {
            response_out->buffer[MAX_HTTP_OUTPUT_BUFFER - 1] = '\0';
        }
        response_out->status_code = -1; // 用 -1 表示请求未成功完成
    }

    // 清理 HTTP 客户端句柄
    esp_http_client_cleanup(client);
    
    // 注意：response_out->buffer 的内存由调用者负责释放，不在本函数内释放
    return success;
}

// 可以替换原始的 http_get_version 函数，或作为新实现
bool http_get_version(const char *token)
{
    if (token == NULL) return false;

    generic_http_response_t response; // 用于接收响应
    char *post_data = NULL;
    cJSON *root = NULL;
    bool success = false;
    instrument_config_t current_config; // 声明以存储从 NVS 读取的配置

    // 从 NVS 加载仪器配置
    // 如果加载失败，可以使用默认值或直接返回失败
    if (load_instrument_config_from_nvs(&current_config) != ESP_OK) {
        ESP_LOGE(OBS_TAG, "Failed to load instrument config from NVS. Using default values.");
        // 设置一些默认值，如果NVS加载失败
        strcpy(current_config.product_code, "default_product");
        strcpy(current_config.platform_name, "default_platform");
        strcpy(current_config.firmware_version, "v0.0.0");
    } else {
        ESP_LOGI(OBS_TAG, "Loaded instrument config from NVS: Product='%s', Platform='%s', Version='%s'",
                 current_config.product_code, current_config.platform_name, current_config.firmware_version);
    }

    // 1. 构建 POST Body (业务逻辑部分) - 现在使用从 NVS 读取的值
    root = cJSON_CreateObject();
    if (!root) {
        ESP_LOGE(OBS_TAG, "Failed to create cJSON root object.");
        return false;
    }
    // 使用从 NVS 加载的值来构建 JSON
    
    //cJSON_AddStringToObject(root, "productcode", PRODUCT_CODE);
    cJSON_AddStringToObject(root, "productcode", current_config.product_code);
    cJSON_AddStringToObject(root, "platformcode", current_config.platform_code);
   // cJSON_AddStringToObject(root, "platformcode", current_config.platform_code);
    cJSON_AddStringToObject(root, "version", current_config.firmware_version); // 使用 firmware_version 作为 "version" 字段
    post_data = cJSON_PrintUnformatted(root);

    if (!post_data) {
        ESP_LOGE(OBS_TAG, "Failed to print cJSON object.");
        cJSON_Delete(root);
        return false;
    }

    const char *target_url = DOWNLOAD_CURRENT_URL;

    // 2. 调用通用 HTTP 请求接口
    if (http_send_request(target_url, HTTP_METHOD_POST, token, "application/json", post_data, &response)) {
        // 请求传输成功，现在检查 HTTP 状态码和响应体
        if (response.status_code == 200 && response.len > 0) {
            // 3. 解析响应体 (业务逻辑部分)
            ESP_LOGI(OBS_TAG, "Received HTTP Status: %d, Body: %s", response.status_code, response.buffer);
            if (parse_ota_response(response.buffer, &g_download_info)) {
                if (g_download_info.code == 0) {
                    if (g_download_info.url[0] == '\0' || g_download_info.file_name[0] == '\0') {
                        ESP_LOGW(OBS_TAG, "Response from %s contains no download info, falling back to %s", target_url, DOWNLOAD_URL);
                    } else {
                        ESP_LOGI(OBS_TAG, "OTA response parsed successfully: Version %s", g_download_info.version);
                        success = true;
                    }
                } else {
                    ESP_LOGW(OBS_TAG, "OTA response business code indicates failure: %d", g_download_info.code);
                }
            } else {
                ESP_LOGW(OBS_TAG, "Failed to parse OTA response from %s, falling back to %s", target_url, DOWNLOAD_URL);
            }
        } else {
            ESP_LOGW(OBS_TAG, "HTTP request failed with status code: %d, body: %s", response.status_code, response.buffer);
        }
    } else {
        ESP_LOGE(OBS_TAG, "HTTP request (transport layer) failed for get_version.");
    }

    if (!success) {
        if (response.buffer) {
            free(response.buffer);
            response.buffer = NULL;
            response.len = 0;
            response.status_code = 0;
        }

        if (http_send_request(DOWNLOAD_URL, HTTP_METHOD_POST, token, "application/json", post_data, &response)) {
            if (response.status_code == 200 && response.len > 0) {
                ESP_LOGI(OBS_TAG, "Fallback HTTP Status: %d, Body: %s", response.status_code, response.buffer);
                if (parse_ota_response(response.buffer, &g_download_info)) {
                    if (g_download_info.code == 0 && g_download_info.url[0] != '\0' && g_download_info.file_name[0] != '\0') {
                        ESP_LOGI(OBS_TAG, "Fallback OTA response parsed successfully: Version %s", g_download_info.version);
                        success = true;
                    } else {
                        ESP_LOGW(OBS_TAG, "Fallback OTA response missing download info or returned failure code: %d", g_download_info.code);
                    }
                } else {
                    ESP_LOGE(OBS_TAG, "Failed to parse fallback OTA response.");
                }
            } else {
                ESP_LOGE(OBS_TAG, "Fallback HTTP request failed with status code: %d, body: %s", response.status_code, response.buffer);
            }
        } else {
            ESP_LOGE(OBS_TAG, "Fallback HTTP request (transport layer) failed for get_version.");
        }
    }

    // 4. 清理资源
    if (post_data) free(post_data);
    if (root) cJSON_Delete(root);
    if (response.buffer) free(response.buffer); // 释放通用接口分配的响应缓冲区
    ESP_LOGI(OBS_TAG, "http_get_version finished.");
    return success;
}


/**
 * @brief 校验本地文件的 MD5
 * @param path 文件路径 (例如 "/usb/update.bin")
 * @param expected_md5 期望的 MD5 字符串 (从 JSON 解析得到的 32 位小写字符串)
 * @return true 校验通过 / false 校验失败
 */
bool verify_file_md5(const char *path, const char *expected_md5) {
    if (path == NULL || expected_md5 == NULL) return false;

    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        ESP_LOGE("MD5", "Failed to open: %s", path);
        return false;
    }

    mbedtls_md5_context ctx;
    mbedtls_md5_init(&ctx);
    mbedtls_md5_starts(&ctx);

    // ⭐ 优化1：加大缓冲区提升 700M 文件的读取效率 (从 1K 改为 32K)
    const size_t buf_size = 32 * 1024; 
    uint8_t *buffer = malloc(buf_size);
    if (!buffer) {
        ESP_LOGE("MD5", "Malloc failed");
        fclose(f);
        mbedtls_md5_free(&ctx);
        return false;
    }

    size_t read_len;
    ESP_LOGI("MD5", "Verifying file, please wait...");
    while ((read_len = fread(buffer, 1, buf_size, f)) > 0) {
        mbedtls_md5_update(&ctx, buffer, read_len);
    }

    // ⭐ 优化2：检查是否因读取错误而停止
    if (ferror(f)) {
        ESP_LOGE("MD5", "File read error occurred!");
    }

    uint8_t digest[16];
    mbedtls_md5_finish(&ctx, digest);
    mbedtls_md5_free(&ctx);
    free(buffer);
    fclose(f);

    // 将二进制转为字符串
    char actual_md5[33];
    for (int i = 0; i < 16; i++) {
        snprintf(&actual_md5[i * 2], 3, "%02x", digest[i]);
    }
    actual_md5[32] = '\0'; // 显式闭合

    // 比对
    if (strcasecmp(actual_md5, expected_md5) == 0) {
        ESP_LOGI("MD5", "✅ PASS: %s", actual_md5);
        return true;
    } else {
        ESP_LOGE("MD5", "❌ FAIL! Expected: %s, Actual: %s", expected_md5, actual_md5);
        return false;
    }
}

/* ===================== 桶文件查询 ===================== */
esp_err_t obs_http_list_bucket(const char *url, const char *save_path)
{
    return obs_http_download(url, save_path);
}

/* ===================== 示例 ===================== */
/*
MQTT回调中获取URL后调用：

obs_http_download(temp_url, "/spiffs/fw.bin");

上传：
obs_http_upload(upload_url, "/spiffs/log.txt");

查询桶：
obs_http_list_bucket(list_url, "/spiffs/list.xml");

*/

