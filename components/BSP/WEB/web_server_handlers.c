/**
 * @file web_server_handlers.c
 * @brief Web服务器处理器实现文件
 *
 * 该文件包含ESP32 Web服务器的HTTP请求处理器函数，
 * 用于处理配置管理、WiFi操作、OTA升级等功能。
 * 主要功能包括：
 * - 仪器配置的获取和保存
 * - WiFi扫描和连接
 * - 数据上传配置管理
 * - 产品和平台信息查询
 * - OTA升级处理
 */

#include "web_server_handlers.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "string.h"
#include "stdlib.h" // For malloc/free
#include "esp_wifi.h" // 用于 wifi_scan_handler, connect_wifi_handler, get_wifi_info_handler
#include "web_pages.h" // 用于 FW_VERSION 和 HW_VERSION
#include "obs_http.h" // 用于 HTTP API 调用
// 引入 wifi_config.h 的相关函数，以便在 connect_wifi_handler 中调用 wifi_apply_config
// 声明 extern 确保可调用
extern esp_err_t wifi_apply_config(const char *ssid, const char *password);

static const char *TAG_WEB_SERVER = "WEB_SERVER_HANDLERS";  ///< 日志标签
static nvs_handle_t config_nvs_handle;  ///< 统一的 NVS 句柄，用于存储配置数据
login_response_t g_strResp = {0};  ///< 全局登录响应结构体
QueueHandle_t ota_queue = NULL;  ///< OTA升级消息队列句柄

// WiFi扫描相关定义
#define MAX_SCAN_RESULTS 20  ///< 最大WiFi扫描结果数量

/**
 * @brief WiFi扫描结果结构体
 */
typedef struct {
    char ssid[33];  ///< SSID字符串，最长32字节 + 终止符
    int8_t rssi;    ///< 信号强度(RSSI)
} scan_result_t;

static scan_result_t g_scan_results[MAX_SCAN_RESULTS];  ///< 全局WiFi扫描结果数组
static int g_scan_count = 0;  ///< 当前扫描到的WiFi数量
static bool scan_done = false;  ///< 扫描完成标志

/**
 * @brief 初始化Web配置的NVS存储
 *
 * 打开NVS命名空间用于存储Web服务器相关的配置数据。
 *
 * @return esp_err_t 操作结果
 *         - ESP_OK: 初始化成功
 *         - ESP_ERR_NVS_NOT_INITIALIZED: NVS未初始化
 *         - ESP_ERR_NVS_NOT_FOUND: 命名空间不存在
 */
esp_err_t init_web_config_nvs(void) {
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &config_nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_WEB_SERVER, "Error (%s) opening NVS handle!", esp_err_to_name(err));
    }
    return err;
}

/**
 * @brief 保存仪器配置到NVS存储
 *
 * 将仪器配置结构体中的数据保存到NVS中，包括平台ID、平台名称、
 * 产品代码和固件版本。
 *
 * @param config 指向仪器配置结构体的指针
 * @return esp_err_t 操作结果
 *         - ESP_OK: 保存成功
 *         - ESP_ERR_INVALID_STATE: NVS句柄未初始化
 *         - ESP_ERR_NVS_*: NVS相关错误
 */
esp_err_t save_instrument_config_to_nvs(const instrument_config_t *config) {
    if (config_nvs_handle == 0) {
        ESP_LOGE(TAG_WEB_SERVER, "NVS handle is not initialized!");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = ESP_OK;

    // 1. 保存平台 ID (发送给服务器用)
    err |= nvs_set_str(config_nvs_handle, NVS_KEY_PLATFORM_CODE, config->platform_id);
    
    // 2. 保存平台名称 (本地展示用) - 新增
    err |= nvs_set_str(config_nvs_handle, NVS_KEY_PLATFORM_NAME, config->platform_name);
    
    // 3. 保存产品代码
    err |= nvs_set_str(config_nvs_handle, NVS_KEY_PRODUCT_CODE, config->product_code);
    
    // 4. 保存固件版本
    err |= nvs_set_str(config_nvs_handle, NVS_KEY_FIRMWARE_VERSION, config->firmware_version);

    // 5. 提交更改
    if (err == ESP_OK) {
        err = nvs_commit(config_nvs_handle);
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG_WEB_SERVER, "保存配置到 NVS 失败: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG_WEB_SERVER, "--- NVS 写入成功 ---");
        ESP_LOGI(TAG_WEB_SERVER, "Stored ID:   %s", config->platform_id);
        ESP_LOGI(TAG_WEB_SERVER, "Stored Name: %s", config->platform_name);
    }

    return err;
}

/**
 * @brief 从NVS加载仪器配置
 *
 * 从NVS存储中读取仪器配置数据，如果某个字段不存在，
 * 则使用默认值填充。
 *
 * @param config 指向仪器配置结构体的指针，用于存储读取的数据
 * @return esp_err_t 操作结果（通常返回ESP_OK，即使某些字段未找到）
 */
esp_err_t load_instrument_config_from_nvs(instrument_config_t *config) {
    esp_err_t err = ESP_OK;
    size_t len;

    // 1. 读取 Platform ID (对应原本的 platform_code 键值，发送服务器用)
    len = sizeof(config->platform_id);
    err = nvs_get_str(config_nvs_handle, NVS_KEY_PLATFORM_CODE, config->platform_id, &len);
    if (err == ESP_ERR_NVS_NOT_FOUND) { 
        strcpy(config->platform_id, ""); 
    } else if (err != ESP_OK) { 
        ESP_LOGE(TAG_WEB_SERVER, "Failed to load platform_id (%s)", esp_err_to_name(err)); 
    }

    // 2. 读取 Platform Name (新增：本地显示用)
    len = sizeof(config->platform_name);
    err = nvs_get_str(config_nvs_handle, NVS_KEY_PLATFORM_NAME, config->platform_name, &len);
    if (err == ESP_ERR_NVS_NOT_FOUND) { 
        strcpy(config->platform_name, "Unknown"); // 找不到时给个默认名称
    } else if (err != ESP_OK) { 
        ESP_LOGE(TAG_WEB_SERVER, "Failed to load platform_name (%s)", esp_err_to_name(err)); 
    }

    // 3. 读取 Product Code
    len = sizeof(config->product_code);
    err = nvs_get_str(config_nvs_handle, NVS_KEY_PRODUCT_CODE, config->product_code, &len);
    if (err == ESP_ERR_NVS_NOT_FOUND) { 
        strcpy(config->product_code, ""); 
    } else if (err != ESP_OK) { 
        ESP_LOGE(TAG_WEB_SERVER, "Failed to load product_code (%s)", esp_err_to_name(err)); 
    }

    // 4. 读取 Firmware Version
    len = sizeof(config->firmware_version);
    err = nvs_get_str(config_nvs_handle, NVS_KEY_FIRMWARE_VERSION, config->firmware_version, &len);
    if (err == ESP_ERR_NVS_NOT_FOUND) { 
        strcpy(config->firmware_version, ""); 
    } else if (err != ESP_OK) { 
        ESP_LOGE(TAG_WEB_SERVER, "Failed to load firmware_version (%s)", esp_err_to_name(err)); 
    }
    
    // 打印加载结果，方便调试确认 ID 和 Name 是否都读到了
    ESP_LOGI(TAG_WEB_SERVER, "NVS Loaded: ID=%s, Name=%s, Version=%s", 
             config->platform_id, config->platform_name, config->firmware_version);

    return ESP_OK; // 即使某个字段没找到，我们也返回 OK 保证程序继续运行
}

/**
 * @brief 从NVS加载数据上传配置
 *
 * 从NVS中读取数据上传相关的配置，包括本地文件路径和上传服务器地址。
 *
 * @param config 指向数据上传配置结构体的指针
 * @return esp_err_t 操作结果
 */
esp_err_t load_data_upload_config_from_nvs(data_upload_config_t *config) {
    esp_err_t err = ESP_OK;
    size_t len;

    // local_file
    len = sizeof(config->local_file);
    err = nvs_get_str(config_nvs_handle, NVS_KEY_LOCAL_FILE, config->local_file, &len);
    if (err == ESP_ERR_NVS_NOT_FOUND) { strcpy(config->local_file, ""); err = ESP_OK; } // Not found, return empty string
    else if (err != ESP_OK) { ESP_LOGE(TAG_WEB_SERVER, "Failed to load local_file (%s)", esp_err_to_name(err)); return err; }

    // upload_server
    len = sizeof(config->upload_server);
    err = nvs_get_str(config_nvs_handle, NVS_KEY_UPLOAD_SERVER, config->upload_server, &len);
    if (err == ESP_ERR_NVS_NOT_FOUND) { strcpy(config->upload_server, ""); err = ESP_OK; } // Not found, return empty string
    else if (err != ESP_OK) { ESP_LOGE(TAG_WEB_SERVER, "Failed to load upload_server (%s)", esp_err_to_name(err)); return err; }
    
    return err;
}

/**
 * @brief 将数据上传配置保存到NVS
 *
 * 保存数据上传配置到NVS存储中。
 *
 * @param config 指向数据上传配置结构体的指针
 * @return esp_err_t 操作结果
 */
esp_err_t save_data_upload_config_to_nvs(const data_upload_config_t *config) {
    esp_err_t err = ESP_OK;
    err |= nvs_set_str(config_nvs_handle, NVS_KEY_LOCAL_FILE, config->local_file);
    err |= nvs_set_str(config_nvs_handle, NVS_KEY_UPLOAD_SERVER, config->upload_server);
    err |= nvs_commit(config_nvs_handle); // 提交数据上传配置
    if (err != ESP_OK) {
        ESP_LOGE(TAG_WEB_SERVER, "Failed to save data upload config to NVS (%s)", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG_WEB_SERVER, "Data upload config saved to NVS.");
    }
    return err;
}


/**
 * @brief HTTP GET处理器：获取仪器配置
 *
 * 处理前端GET /get_instrument_config请求，返回JSON格式的仪器配置信息，
 * 包括平台ID、平台名称、产品代码、固件版本以及数据上传配置。
 *
 * @param req HTTP请求结构体指针
 * @return esp_err_t 处理结果
 */
esp_err_t get_instrument_config_handler(httpd_req_t *req) {
    instrument_config_t config;
    // 这里不检查load_instrument_config_from_nvs的返回值，因为我们会填充默认空值
    load_instrument_config_from_nvs(&config); 
    
    // 合并数据上传配置
    data_upload_config_t data_cfg;
    load_data_upload_config_from_nvs(&data_cfg); // 忽略错误，如果找不到就用空字符串

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "创建JSON对象失败");
        return ESP_FAIL;
    }

    cJSON_AddStringToObject(root, "platform_id", config.platform_id);
    cJSON_AddStringToObject(root, "platform_name", config.platform_name);
    cJSON_AddStringToObject(root, "product_code", config.product_code);
    cJSON_AddStringToObject(root, "firmware_version", config.firmware_version);
    cJSON_AddStringToObject(root, "localFile", data_cfg.local_file);
    cJSON_AddStringToObject(root, "uploadServer", data_cfg.upload_server);

    char *json_string = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!json_string) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "JSON序列化失败");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_string);
    free(json_string);
    return ESP_OK;
}

/**
 * @brief HTTP POST处理器：保存仪器配置
 *
 * 处理前端POST /save_instrument_config请求，解析JSON数据并保存到NVS。
 * 支持更新平台ID、平台名称、产品代码、固件版本以及数据上传配置。
 *
 * @param req HTTP请求结构体指针
 * @return esp_err_t 处理结果
 */
esp_err_t save_instrument_config_handler(httpd_req_t *req) {
    char *buf;
    int total_len = req->content_len;
    int cur_len = 0;
    if (total_len >= 512) { // 防止过大的请求体，根据实际情况调整
        ESP_LOGE(TAG_WEB_SERVER, "Request content length too large: %d", total_len);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "请求内容过大");
        return ESP_FAIL;
    }
    buf = (char *)malloc(total_len + 1);
    if (!buf) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "内存分配失败");
        return ESP_FAIL;
    }

    while (cur_len < total_len) {
        int received = httpd_req_recv(req, buf + cur_len, total_len - cur_len);
        if (received <= 0) {
            free(buf);
            if (received == HTTPD_SOCK_ERR_TIMEOUT) {
                httpd_resp_send_408(req);
            }
            return ESP_FAIL;
        }
        cur_len += received;
    }
    buf[total_len] = '\0'; // Null-terminate the received content

    cJSON *root = cJSON_Parse(buf);
    free(buf); // Release buffer after parsing
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "JSON解析失败");
        return ESP_FAIL;
    }

    instrument_config_t instr_config;
    data_upload_config_t data_config;

    // 先加载已有配置，再用新的覆盖，避免未提交的字段被清空
    load_instrument_config_from_nvs(&instr_config); 
    load_data_upload_config_from_nvs(&data_config);

    cJSON *id_json = cJSON_GetObjectItemCaseSensitive(root, "platform_id");
    cJSON *name_json = cJSON_GetObjectItemCaseSensitive(root, "platform_code");
    cJSON *product_code_json = cJSON_GetObjectItemCaseSensitive(root, "product_code");
    cJSON *firmware_version_json = cJSON_GetObjectItemCaseSensitive(root, "firmware_version");
    cJSON *local_file_json = cJSON_GetObjectItemCaseSensitive(root, "localFile");
    cJSON *upload_server_json = cJSON_GetObjectItemCaseSensitive(root, "uploadServer");

    if (cJSON_IsString(id_json) && id_json->valuestring) {
        strncpy(instr_config.platform_id, id_json->valuestring, sizeof(instr_config.platform_id) - 1);
    }
    if (cJSON_IsString(name_json) && name_json->valuestring) {
        strncpy(instr_config.platform_name, name_json->valuestring, sizeof(instr_config.platform_name) - 1);
    }

    if (cJSON_IsString(product_code_json) && (product_code_json->valuestring != NULL)) {
        strncpy(instr_config.product_code, product_code_json->valuestring, sizeof(instr_config.product_code) - 1);
        instr_config.product_code[sizeof(instr_config.product_code) - 1] = '\0';
    }

    if (cJSON_IsString(firmware_version_json) && (firmware_version_json->valuestring != NULL)) {
        strncpy(instr_config.firmware_version, firmware_version_json->valuestring, sizeof(instr_config.firmware_version) - 1);
        instr_config.firmware_version[sizeof(instr_config.firmware_version) - 1] = '\0';
    }
    
    if (cJSON_IsString(local_file_json) && (local_file_json->valuestring != NULL)) {
        strncpy(data_config.local_file, local_file_json->valuestring, sizeof(data_config.local_file) - 1);
        data_config.local_file[sizeof(data_config.local_file) - 1] = '\0';
    }

    if (cJSON_IsString(upload_server_json) && (upload_server_json->valuestring != NULL)) {
        strncpy(data_config.upload_server, upload_server_json->valuestring, sizeof(data_config.upload_server) - 1);
        data_config.upload_server[sizeof(data_config.upload_server) - 1] = '\0';
    }

    cJSON_Delete(root);

    // 1. 执行保存到 NVS
    esp_err_t err_instr = save_instrument_config_to_nvs(&instr_config);
    esp_err_t err_data = save_data_upload_config_to_nvs(&data_config);

    if (err_instr != ESP_OK || err_data != ESP_OK) {
        ESP_LOGE(TAG_WEB_SERVER, "保存配置到 NVS 失败! Instr err: %s, Data err: %s",
                 esp_err_to_name(err_instr), esp_err_to_name(err_data));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "保存配置失败");
        return ESP_FAIL;
    }

    // 2. 使用 ESP_LOGI 详细打印保存的所有数据
    ESP_LOGI(TAG_WEB_SERVER, "==============================================");
    ESP_LOGI(TAG_WEB_SERVER, "✅ 配置已同步:");
    ESP_LOGI(TAG_WEB_SERVER, ">> [发送用] 平台 ID:   %s", instr_config.platform_id);
    ESP_LOGI(TAG_WEB_SERVER, ">> [本地用] 平台名称: %s", instr_config.platform_name);
    ESP_LOGI(TAG_WEB_SERVER, ">> 固件版本:          %s", instr_config.firmware_version);
    ESP_LOGI(TAG_WEB_SERVER, "==============================================");

    // 3. 返回响应给 Web 前端
    httpd_resp_sendstr(req, "配置保存成功！");
    return ESP_OK;
}

/**
 * @brief WiFi扫描完成回调函数
 *
 * 当WiFi扫描完成时被调用的回调函数，处理扫描结果，
 * 去重并按信号强度排序。
 *
 * @param arg 用户参数（未使用）
 * @param event_base 事件基础
 * @param event_id 事件ID
 * @param event_data 事件数据
 */
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
        // 去重：检查是否已存在相同的SSID
        for (int j = 0; j < g_scan_count; j++) {
            if (strcmp((char*)ap_records[i].ssid, g_scan_results[j].ssid) == 0) {
                found = true;
                // 如果找到相同SSID，保留信号更强的
                if (ap_records[i].rssi > g_scan_results[j].rssi)
                    g_scan_results[j].rssi = ap_records[i].rssi;
                break;
            }
        }
        // 添加新的SSID
        if (!found) {
            strncpy(g_scan_results[g_scan_count].ssid, (char*)ap_records[i].ssid, 32);
            g_scan_results[g_scan_count].ssid[32] = 0;
            g_scan_results[g_scan_count].rssi = ap_records[i].rssi;
            g_scan_count++;
        }
    }
    scan_done = true;
}

/**
 * @brief HTTP处理器：WiFi扫描
 *
 * 处理前端WiFi扫描请求，启动异步WiFi扫描，等待结果并返回JSON格式的WiFi列表。
 * 结果按信号强度降序排序。
 *
 * @param req HTTP请求结构体指针
 * @return esp_err_t 处理结果
 */
esp_err_t wifi_scan_handler(httpd_req_t *req)
{
    scan_done = false;

    wifi_scan_config_t cfg = {0};
    esp_wifi_scan_start(&cfg, false); // 异步扫描
    
    // 由于 wifi_scan_done_cb 是一个回调，为了在 HTTP 响应前获取结果，我们需要在这里等待它
    // 或者将扫描结果缓存起来，在回调中更新
    // 这里采用简单等待方式，实际应用中可以考虑更复杂的异步机制
    esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_SCAN_DONE, &wifi_scan_done_cb, NULL);

    // 等待扫描完成（最多 3 秒）
    int wait_ms = 0;
    while (!scan_done && wait_ms < 3000) {
        vTaskDelay(pdMS_TO_TICKS(100));
        wait_ms += 100;
    }
    esp_event_handler_unregister(WIFI_EVENT, WIFI_EVENT_SCAN_DONE, &wifi_scan_done_cb); // 注册后要解除注册

    // ===== 按 RSSI 排序（信号强优先）=====
    for (int i = 0; i < g_scan_count - 1; i++) {
        for (int j = i + 1; j < g_scan_count; j++) {
            if (g_scan_results[j].rssi > g_scan_results[i].rssi) {
                // 交换位置
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

/**
 * @brief HTTP处理器：连接WiFi
 *
 * 处理前端WiFi连接请求，解析SSID和密码，调用WiFi配置函数进行连接。
 *
 * @param req HTTP请求结构体指针
 * @return esp_err_t 处理结果
 */
esp_err_t connect_wifi_handler(httpd_req_t *req)
{
    char buf[256] = {0};
    int total_len = req->content_len;
    int cur_len = 0;
    int received = 0;

    if (total_len >= sizeof(buf)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "内容过长");
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

    ESP_LOGI(TAG_WEB_SERVER, "POST DATA: %s", buf);

    // 🔥 解析 JSON
    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "无效的JSON格式");
        return ESP_FAIL;
    }

    const cJSON *ssid_item = cJSON_GetObjectItem(root, "ssid");
    const cJSON *pwd_item  = cJSON_GetObjectItem(root, "password");

    if (!cJSON_IsString(ssid_item) || !cJSON_IsString(pwd_item)) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "无效的参数");
        return ESP_FAIL;
    }

    const char *ssid = ssid_item->valuestring;
    const char *password = pwd_item->valuestring;

    ESP_LOGI(TAG_WEB_SERVER, "SSID: %s", ssid);
    ESP_LOGI(TAG_WEB_SERVER, "Password: %s", password);

    if (strlen(ssid) == 0) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "SSID不能为空");
        return ESP_FAIL;
    }

    wifi_apply_config(ssid, password); // 调用 wifi_config.c 中的函数

    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"connecting\"}"); // JSON 字符串中的引号需要转义

    return ESP_OK;
}

/**
 * @brief HTTP处理器：获取USB文件列表
 *
 * 处理前端USB文件列表请求，扫描/disk目录下的文件并返回JSON数组。
 *
 * @param req HTTP请求结构体指针
 * @return esp_err_t 处理结果
 */
esp_err_t usb_files_handler(httpd_req_t *req)
{
    DIR *dir;
    struct dirent *entry;

    cJSON *root = cJSON_CreateArray();
    ESP_LOGI(TAG_WEB_SERVER, "usb_files_handler CALLED");
    if (!root)
    {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "创建JSON数组失败");
        return ESP_FAIL;
    }

    dir = opendir("/disk"); // 假设 /disk 是您的 USB 挂载点
    if (dir == NULL)
    {
        ESP_LOGE(TAG_WEB_SERVER, "Failed to open /disk");
        cJSON_Delete(root);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "[]"); // 没有文件时返回空数组
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

/**
 * @brief HTTP处理器：获取数据上传配置
 *
 * 处理前端获取数据上传配置的请求，返回本地文件路径和上传服务器地址。
 *
 * @param req HTTP请求结构体指针
 * @return esp_err_t 处理结果
 */
esp_err_t get_config_handler(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();

    data_upload_config_t cfg;
    // 如果NVS没有找到配置，load_data_upload_config_from_nvs会填充空字符串，无需额外处理
    load_data_upload_config_from_nvs(&cfg); 

    cJSON_AddStringToObject(root, "localFile", cfg.local_file);
    cJSON_AddStringToObject(root, "uploadServer", cfg.upload_server);

    char *out = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, out, strlen(out));

    cJSON_Delete(root);
    free(out);
    return ESP_OK;
}

/**
 * @brief HTTP处理器：保存数据上传配置
 *
 * 处理前端保存数据上传配置的请求，更新本地文件路径和上传服务器地址到NVS。
 *
 * @param req HTTP请求结构体指针
 * @return esp_err_t 处理结果
 */
esp_err_t save_config_handler(httpd_req_t *req)
{
    char buf[128];
    int ret = httpd_req_recv(req, buf, req->content_len);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "接收数据失败");
        return ESP_FAIL;
    }
    buf[ret] = 0;

    cJSON *json = cJSON_Parse(buf);
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "无效的JSON格式");
        return ESP_FAIL;
    }

    const cJSON *local = cJSON_GetObjectItem(json, "localFile");
    const cJSON *server = cJSON_GetObjectItem(json, "uploadServer");

    data_upload_config_t config;
    load_data_upload_config_from_nvs(&config); // 先加载现有配置

    if (local && server) {
        strncpy(config.local_file, local->valuestring, sizeof(config.local_file)-1);
        config.local_file[sizeof(config.local_file)-1] = '\0';
        strncpy(config.upload_server, server->valuestring, sizeof(config.upload_server)-1);
        config.upload_server[sizeof(config.upload_server)-1] = '\0';
    }
    ESP_LOGI(TAG_WEB_SERVER, "local: %s upload_server: %s", config.local_file, config.upload_server);
    
    esp_err_t save_err = save_data_upload_config_to_nvs(&config);
    cJSON_Delete(json);

    if (save_err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "保存数据配置失败");
        return ESP_FAIL;
    }

    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

/**
 * @brief HTTP处理器：获取WiFi连接信息
 *
 * 处理前端获取当前WiFi连接信息的请求，返回SSID和密码。
 *
 * @param req HTTP请求结构体指针
 * @return esp_err_t 处理结果
 */
esp_err_t get_wifi_info_handler(httpd_req_t *req)
{
    wifi_config_t wifi_cfg;
    memset(&wifi_cfg, 0, sizeof(wifi_cfg));

    cJSON *root = cJSON_CreateObject();

    // 从 NVS 读取保存的 WiFi 配置，而不是直接从 esp_wifi_get_config 获取，
    // 因为 esp_wifi_get_config 可能只返回当前活跃的配置。
    // 在 wifi_apply_config 中，应该把连接的 SSID 和 Password 存入 NVS。
    // 但目前来看，get_wifi_info_handler 应该返回当前连接的信息，或者最后保存的信息。
    // 这里暂时保持直接从 esp_wifi_get_config 获取，这需要 WiFi 处于 STA 模式且配置已应用。
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

/**
 * @brief HTTP处理器：获取产品列表
 *
 * 处理前端获取产品列表的请求，从缓存的HTTP响应中返回数据。
 * 如果缓存为空，返回404错误。
 *
 * @param req HTTP请求结构体指针
 * @return esp_err_t 处理结果
 */
esp_err_t get_product_list_handler(httpd_req_t *req) {
    // ⭐ 每次请求都重新获取产品列表，而不是依赖缓存
    if (!http_get_all_products(g_strResp.token)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "获取产品列表失败");
        return ESP_FAIL;
    }

    // 检查响应是否为空
    if (g_http_resp.buffer == NULL || strlen((char*)g_http_resp.buffer) == 0) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "产品列表为空");
        return ESP_FAIL;
    }

    // ⭐ 设置响应头为 JSON 格式
    httpd_resp_set_type(req, "application/json");

    // ⭐ 跨域设置（如果需要，防止某些浏览器拦截）
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    // ⭐ 发送数据 (使用 g_http_resp.len 长度更精准)
    return httpd_resp_send(req, (const char *)g_http_resp.buffer, g_http_resp.len);
}

/**
 * @brief HTTP处理器：获取平台列表
 *
 * 处理前端GET /api/get_platforms?id=xxx请求，根据产品ID获取对应的平台列表。
 * 需要网络连接和有效的token。
 *
 * @param req HTTP请求结构体指针
 * @return esp_err_t 处理结果
 */
esp_err_t get_platforms_handler(httpd_req_t *req) {
    char buf[128];
    char id_str[16] = {0}; // 稍微加大缓冲区，确保 64 位 ID 安全
    esp_err_t err;

    // 1. 预检：检查 STA 是否已连接到外网 (可选，依赖你的事件组定义)
    /*
    EventBits_t bits = xEventGroupGetBits(s_wifi_event_group);
    if (!(bits & WIFI_CONNECTED_BIT)) {
        ESP_LOGW("HTTP_SERVER", "STA 未联网，拒绝请求");
        httpd_resp_set_status(req, "503 Service Unavailable");
        return httpd_resp_sendstr(req, "{\"code\":-1, \"msg\":\"WiFi未连接到互联网\"}");
    }
    */

    // 2. 解析 URL 参数
    err = httpd_req_get_url_query_str(req, buf, sizeof(buf));
    if (err != ESP_OK) {
        ESP_LOGW("HTTP_SERVER", "URL 无查询参数");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "缺少查询参数");
        return ESP_FAIL;
    }

    // 3. 提取 ID 字段
    if (httpd_query_key_value(buf, "id", id_str, sizeof(id_str)) != ESP_OK) {
        ESP_LOGW("HTTP_SERVER", "参数中缺少 ID");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "需要ID参数");
        return ESP_FAIL;
    }

    // 4. 将字符串安全转为 int64_t
    int64_t product_id = atoll(id_str);
    if (product_id <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "无效的产品ID");
        return ESP_FAIL;
    }

    
    ESP_LOGI("HTTP_SERVER", "正在代发请求：获取产品 ID %lld 的平台列表", product_id);
    // 检查 STA 是否分配到了 IP 地址
        esp_netif_ip_info_t ip_info;
        esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (esp_netif_get_ip_info(netif, &ip_info) != ESP_OK || ip_info.ip.addr == 0) {
            ESP_LOGE("HTTP_SERVER", "STA 未就绪（无IP），放弃请求外网");
            httpd_resp_set_status(req, "503 Service Unavailable");
            return httpd_resp_sendstr(req, "{\"code\":-1, \"msg\":\"ESP32 is not connected to Internet\"}");
        }
    // 5. 调用外部 API 请求函数 (使用传入的 Token)
    // 假设 g_strResp.token 是你的局部/全局变量
    if (http_get_product_platforms(g_strResp.token, product_id)) {
        
        // 确保 buffer 中有数据
        if (g_http_resp.buffer && strlen((char*)g_http_resp.buffer) > 0) {
            httpd_resp_set_type(req, "application/json");
            // 设置跨域（如果前端调试需要）
            httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*"); 
            return httpd_resp_send(req, (char*)g_http_resp.buffer, strlen((char*)g_http_resp.buffer));
        }
    }

    // 6. 走到这里说明后端 API 请求失败或超时
    ESP_LOGE("HTTP_SERVER", "服务器 API 请求失败");
    httpd_resp_set_status(req, "502 Bad Gateway");
    return httpd_resp_sendstr(req, "{\"code\":-1, \"msg\":\"Remote server timeout or error\"}");
}

/**
 * @brief HTTP处理器：获取版本列表
 *
 * 处理前端GET /api/get_versions?id=xxx请求，根据平台ID获取对应的版本列表。
 *
 * @param req HTTP请求结构体指针
 * @return esp_err_t 处理结果
 */
esp_err_t get_versions_handler(httpd_req_t *req) {
    char buf[128];
    char id_str[10];
    
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        if (httpd_query_key_value(buf, "id", id_str, sizeof(id_str)) == ESP_OK) {
            int64_t platform_id = atoll(id_str);
            
            // 这里调用你的第3个函数，你可以根据需要调整参数
            if (http_get_platform_versions(g_strResp.token, platform_id)) {
                httpd_resp_set_type(req, "application/json");
                return httpd_resp_send(req, (char*)g_http_resp.buffer, strlen((char*)g_http_resp.buffer));
            }
        }
    }
    httpd_resp_send_500(req);
    return ESP_FAIL;
}

/**
 * @brief HTTP处理器：OTA升级
 *
 * 处理前端OTA升级请求，支持最新版本升级和配置保存。
 * 如果请求升级最新版本，则将升级消息发送到OTA队列。
 *
 * @param req HTTP请求结构体指针
 * @return esp_err_t 处理结果
 */
esp_err_t update_handler(httpd_req_t *req) {
    char buf[512];
    int ret = httpd_req_recv(req, buf, req->content_len);
    if (ret <= 0) return ESP_FAIL;
    buf[ret] = '\0';

    ESP_LOGI("WEB_SERVER", "收到原始 JSON: %s", buf); // ⭐ 打印出来看看到底有没有 id

    cJSON *root = cJSON_Parse(buf);
    if (!root) return ESP_FAIL;

    cJSON *ver_item = cJSON_GetObjectItem(root, "firmware_version");
    
    if (ver_item && strcmp(ver_item->valuestring, "latest") == 0) {
        // 处理OTA升级请求
        if (ota_queue != NULL) {
            ota_msg_t msg = {0};
            
            cJSON *f_id = cJSON_GetObjectItem(root, "platform_id");
            
            // ⭐ 健壮性解析：处理数字或字符串类型的 ID
            if (cJSON_IsNumber(f_id)) {
                msg.platform_id = (int64_t)f_id->valuedouble;
            } else if (cJSON_IsString(f_id)) {
                msg.platform_id = atoll(f_id->valuestring);
            } else {
                msg.platform_id = 0; 
            }

            ESP_LOGW("WEB_SERVER", "解析后的 Platform ID: %lld", msg.platform_id);
            // 将OTA消息发送到队列
            if (xQueueSend(ota_queue, &msg, 0) == pdPASS) {
                httpd_resp_sendstr(req, "OK: 升级指令已加入队列");
            } else {
                httpd_resp_send_500(req); 
            }
        }
        cJSON_Delete(root);
        return ESP_OK;
    }

    // 原有保存逻辑...
    httpd_resp_sendstr(req, "配置已保存");
    cJSON_Delete(root);
    return ESP_OK;
}
