#include "web_server_handlers.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "string.h"
#include "stdlib.h" // For malloc/free

static const char *TAG_WEB_SERVER = "WEB_SERVER_HANDLERS";
static nvs_handle_t config_nvs_handle;

// 初始化 NVS
esp_err_t init_web_config_nvs(void) {
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &config_nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_WEB_SERVER, "Error (%s) opening NVS handle!", esp_err_to_name(err));
    }
    return err;
}

// 保存仪器配置到 NVS
esp_err_t save_instrument_config_to_nvs(const instrument_config_t *config) {
    esp_err_t err = ESP_OK;
    err |= nvs_set_str(config_nvs_handle, NVS_KEY_PLATFORM_CODE, config->platform_code);
    err |= nvs_set_str(config_nvs_handle, NVS_KEY_PRODUCT_CODE, config->product_code);
    err |= nvs_set_str(config_nvs_handle, NVS_KEY_FIRMWARE_VERSION, config->firmware_version);
    err |= nvs_set_str(config_nvs_handle, NVS_KEY_LOCAL_FILE, config->local_file);
    err |= nvs_set_str(config_nvs_handle, NVS_KEY_UPLOAD_SERVER, config->upload_server);
    err |= nvs_commit(config_nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_WEB_SERVER, "Failed to save instrument config to NVS (%s)", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG_WEB_SERVER, "Instrument config saved to NVS.");
    }
    return err;
}

// 从 NVS 加载仪器配置
esp_err_t load_instrument_config_from_nvs(instrument_config_t *config) {
    esp_err_t err = ESP_OK;
    size_t len;

    // platform_code
    len = sizeof(config->platform_code);
    err = nvs_get_str(config_nvs_handle, NVS_KEY_PLATFORM_CODE, config->platform_code, &len);
    if (err == ESP_ERR_NVS_NOT_FOUND) { strcpy(config->platform_code, ""); err = ESP_OK; }
    else if (err != ESP_OK) { ESP_LOGE(TAG_WEB_SERVER, "Failed to load platform_code (%s)", esp_err_to_name(err)); return err; }

    // product_code
    len = sizeof(config->product_code);
    err = nvs_get_str(config_nvs_handle, NVS_KEY_PRODUCT_CODE, config->product_code, &len);
    if (err == ESP_ERR_NVS_NOT_FOUND) { strcpy(config->product_code, ""); err = ESP_OK; }
    else if (err != ESP_OK) { ESP_LOGE(TAG_WEB_SERVER, "Failed to load product_code (%s)", esp_err_to_name(err)); return err; }

    // firmware_version
    len = sizeof(config->firmware_version);
    err = nvs_get_str(config_nvs_handle, NVS_KEY_FIRMWARE_VERSION, config->firmware_version, &len);
    if (err == ESP_ERR_NVS_NOT_FOUND) { strcpy(config->firmware_version, ""); err = ESP_OK; }
    else if (err != ESP_OK) { ESP_LOGE(TAG_WEB_SERVER, "Failed to load firmware_version (%s)", esp_err_to_name(err)); return err; }

    // local_file
    len = sizeof(config->local_file);
    err = nvs_get_str(config_nvs_handle, NVS_KEY_LOCAL_FILE, config->local_file, &len);
    if (err == ESP_ERR_NVS_NOT_FOUND) { strcpy(config->local_file, ""); err = ESP_OK; }
    else if (err != ESP_OK) { ESP_LOGE(TAG_WEB_SERVER, "Failed to load local_file (%s)", esp_err_to_name(err)); return err; }

    // upload_server
    len = sizeof(config->upload_server);
    err = nvs_get_str(config_nvs_handle, NVS_KEY_UPLOAD_SERVER, config->upload_server, &len);
    if (err == ESP_ERR_NVS_NOT_FOUND) { strcpy(config->upload_server, ""); err = ESP_OK; }
    else if (err != ESP_OK) { ESP_LOGE(TAG_WEB_SERVER, "Failed to load upload_server (%s)", esp_err_to_name(err)); return err; }
    
    return err;
}


/* HTTP GET handler for /get_instrument_config */
esp_err_t get_instrument_config_handler(httpd_req_t *req) {
    instrument_config_t config;
    if (load_instrument_config_from_nvs(&config) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to load config from NVS");
        return ESP_FAIL;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to create JSON object");
        return ESP_FAIL;
    }

    cJSON_AddStringToObject(root, "platform_code", config.platform_code);
    cJSON_AddStringToObject(root, "product_code", config.product_code);
    cJSON_AddStringToObject(root, "firmware_version", config.firmware_version);
    cJSON_AddStringToObject(root, "localFile", config.local_file);
    cJSON_AddStringToObject(root, "uploadServer", config.upload_server);

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
    if (total_len >= 256) { // 防止过大的请求体，根据实际情况调整
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

    instrument_config_t config;
    // 先加载已有配置，再用新的覆盖，避免未提交的字段被清空
    load_instrument_config_from_nvs(&config); 

    cJSON *platform_code_json = cJSON_GetObjectItemCaseSensitive(root, "platform_code");
    cJSON *product_code_json = cJSON_GetObjectItemCaseSensitive(root, "product_code");
    cJSON *firmware_version_json = cJSON_GetObjectItemCaseSensitive(root, "firmware_version");
    cJSON *local_file_json = cJSON_GetObjectItemCaseSensitive(root, "localFile");
    cJSON *upload_server_json = cJSON_GetObjectItemCaseSensitive(root, "uploadServer");

    if (cJSON_IsString(platform_code_json) && (platform_code_json->valuestring != NULL)) {
        strncpy(config.platform_code, platform_code_json->valuestring, sizeof(config.platform_code) - 1);
        config.platform_code[sizeof(config.platform_code) - 1] = '\0';
    }

    if (cJSON_IsString(product_code_json) && (product_code_json->valuestring != NULL)) {
        strncpy(config.product_code, product_code_json->valuestring, sizeof(config.product_code) - 1);
        config.product_code[sizeof(config.product_code) - 1] = '\0';
    }

    if (cJSON_IsString(firmware_version_json) && (firmware_version_json->valuestring != NULL)) {
        strncpy(config.firmware_version, firmware_version_json->valuestring, sizeof(config.firmware_version) - 1);
        config.firmware_version[sizeof(config.firmware_version) - 1] = '\0';
    }
    
    if (cJSON_IsString(local_file_json) && (local_file_json->valuestring != NULL)) {
        strncpy(config.local_file, local_file_json->valuestring, sizeof(config.local_file) - 1);
        config.local_file[sizeof(config.local_file) - 1] = '\0';
    }

    if (cJSON_IsString(upload_server_json) && (upload_server_json->valuestring != NULL)) {
        strncpy(config.upload_server, upload_server_json->valuestring, sizeof(config.upload_server) - 1);
        config.upload_server[sizeof(config.upload_server) - 1] = '\0';
    }

    cJSON_Delete(root);

    if (save_instrument_config_to_nvs(&config) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save config to NVS");
        return ESP_FAIL;
    }

    httpd_resp_sendstr(req, "Instrument configuration saved.");
    return ESP_OK;
}

// TODO: 你需要在你的 HTTP 服务器初始化代码中注册这些 URI 处理器。
// 例如，在 app_main 或你的 web server start 函数中：
/*
void register_instrument_config_handlers(httpd_handle_t server) {
    httpd_uri_t get_instr_config_uri = {
        .uri       = "/get_instrument_config",
        .method    = HTTP_GET,
        .handler   = get_instrument_config_handler,
        .user_cb   = NULL
    };
    httpd_register_uri_handler(server, &get_instr_config_uri);

    httpd_uri_t save_instr_config_uri = {
        .uri       = "/save_instrument_config",
        .method    = HTTP_POST,
        .handler   = save_instrument_config_handler,
        .user_cb   = NULL
    };
    httpd_register_uri_handler(server, &save_instr_config_uri);

    // 确保也注册了 /usb_files, /get_wifi_info, /scan, /connect_wifi 等现有处理器
    // 例如：
    // httpd_uri_t usb_files_uri = { ... }; httpd_register_uri_handler(server, &usb_files_uri);
}
*/
