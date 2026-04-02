#include "web_server_handlers.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "string.h"
#include "stdlib.h" // For malloc/free
#include "esp_wifi.h" // 用于 wifi_scan_handler, connect_wifi_handler, get_wifi_info_handler

// 引入 wifi_config.h 的相关函数，以便在 connect_wifi_handler 中调用 wifi_apply_config
// 声明 extern 确保可调用
extern esp_err_t wifi_apply_config(const char *ssid, const char *password);

static const char *TAG_WEB_SERVER = "WEB_SERVER_HANDLERS";
static nvs_handle_t config_nvs_handle; // 统一的 NVS 句柄
login_response_t g_strResp = {0};

// 用于 wifi_scan_handler
#define MAX_SCAN_RESULTS 20
typedef struct {
    char ssid[33];  // SSID + '\0'
    int8_t rssi;
} scan_result_t;

static scan_result_t g_scan_results[MAX_SCAN_RESULTS];
static int g_scan_count = 0;
static bool scan_done = false;

// 初始化 NVS
esp_err_t init_web_config_nvs(void) {
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &config_nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_WEB_SERVER, "Error (%s) opening NVS handle!", esp_err_to_name(err));
    }
    return err;
}

// 保存仪器配置到 NVS
// 修改后的保存函数
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

// 从 NVS 加载仪器配置
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

// 从 NVS 加载数据上传配置
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

// 将数据上传配置保存到 NVS
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


/* HTTP GET handler for /get_instrument_config */
esp_err_t get_instrument_config_handler(httpd_req_t *req) {
    instrument_config_t config;
    // 这里不检查load_instrument_config_from_nvs的返回值，因为我们会填充默认空值
    load_instrument_config_from_nvs(&config); 
    
    // 合并数据上传配置
    data_upload_config_t data_cfg;
    load_data_upload_config_from_nvs(&data_cfg); // 忽略错误，如果找不到就用空字符串

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to create JSON object");
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
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to print JSON");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_string);
    free(json_string);
    return ESP_OK;
}

/* HTTP POST handler for /save_instrument_config */
esp_err_t save_instrument_config_handler(httpd_req_t *req) {
    char *buf;
    int total_len = req->content_len;
    int cur_len = 0;
    if (total_len >= 512) { // 防止过大的请求体，根据实际情况调整
        ESP_LOGE(TAG_WEB_SERVER, "Request content length too large: %d", total_len);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Content too large");
        return ESP_FAIL;
    }
    buf = (char *)malloc(total_len + 1);
    if (!buf) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to allocate memory for request body");
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
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Failed to parse JSON");
        return ESP_FAIL;
    }

    instrument_config_t instr_config;
    data_upload_config_t data_config;

    // 先加载已有配置，再用新的覆盖，避免未提交的字段被清空
    load_instrument_config_from_nvs(&instr_config); 
    load_data_upload_config_from_nvs(&data_config);

    cJSON *id_json = cJSON_GetObjectItemCaseSensitive(root, "platform_id");
    cJSON *name_json = cJSON_GetObjectItemCaseSensitive(root, "platform_name");
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
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save config to NVS");
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
    httpd_resp_sendstr(req, "Configuration saved successfully and logged.");
    return ESP_OK;
}

// 扫描完成回调 (从 wifi_config.c 移动过来)
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

// wifi_scan_handler (从 wifi_config.c 移动过来)
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

// connect_wifi_handler (从 wifi_config.c 移动过来)
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

    ESP_LOGI(TAG_WEB_SERVER, "POST DATA: %s", buf);

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

    ESP_LOGI(TAG_WEB_SERVER, "SSID: %s", ssid);
    ESP_LOGI(TAG_WEB_SERVER, "Password: %s", password);

    if (strlen(ssid) == 0) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "SSID empty");
        return ESP_FAIL;
    }

    wifi_apply_config(ssid, password); // 调用 wifi_config.c 中的函数

    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"connecting\"}"); // JSON 字符串中的引号需要转义

    return ESP_OK;
}

// usb_files_handler (从 wifi_config.c 移动过来)
esp_err_t usb_files_handler(httpd_req_t *req)
{
    DIR *dir;
    struct dirent *entry;

    cJSON *root = cJSON_CreateArray();
    ESP_LOGI(TAG_WEB_SERVER, "usb_files_handler CALLED");
    if (!root)
    {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "JSON create failed");
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

// get_config_handler (用于获取数据上传配置，从 wifi_config.c 移动过来)
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

// save_config_handler (用于保存数据上传配置，从 wifi_config.c 移动过来)
esp_err_t save_config_handler(httpd_req_t *req)
{
    char buf[128];
    int ret = httpd_req_recv(req, buf, req->content_len);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive data");
        return ESP_FAIL;
    }
    buf[ret] = 0;

    cJSON *json = cJSON_Parse(buf);
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
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
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save data config to NVS");
        return ESP_FAIL;
    }

    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

// get_wifi_info_handler (从 wifi_config.c 移动过来)
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

// 处理前端获取产品列表的请求
esp_err_t get_product_list_handler(httpd_req_t *req) {
    // 检查缓存是否为空
    if (g_http_resp.buffer == NULL || strlen((char*)g_http_resp.buffer) == 0) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "数据未同步，请先按 KEY0");
        return ESP_FAIL;
    }

    // ⭐ 设置响应头为 JSON 格式
    httpd_resp_set_type(req, "application/json");
    
    // ⭐ 跨域设置（如果需要，防止某些浏览器拦截）
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    // ⭐ 发送数据 (使用 g_http_resp.len 长度更精准)
    return httpd_resp_send(req, (const char *)g_http_resp.buffer, g_http_resp.len);
}

/* 处理前端：GET /api/get_platforms?id=4 */
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
        return httpd_resp_sendstr(req, "{\"code\":-1, \"msg\":\"WiFi not connected to internet\"}");
    }
    */

    // 2. 解析 URL 参数
    err = httpd_req_get_url_query_str(req, buf, sizeof(buf));
    if (err != ESP_OK) {
        ESP_LOGW("HTTP_SERVER", "URL 无查询参数");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing Query String");
        return ESP_FAIL;
    }

    // 3. 提取 ID 字段
    if (httpd_query_key_value(buf, "id", id_str, sizeof(id_str)) != ESP_OK) {
        ESP_LOGW("HTTP_SERVER", "参数中缺少 ID");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "ID Parameter Required");
        return ESP_FAIL;
    }

    // 4. 将字符串安全转为 int64_t
    int64_t product_id = atoll(id_str);
    if (product_id <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid Product ID");
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

/* 处理前端：GET /api/get_versions?id=13 */
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