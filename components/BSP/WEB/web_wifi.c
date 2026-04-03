#include "web_wifi.h"
#include "web_pages.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "../../main/APP/wifi_config.h"

static const char *TAG = "WEB_WIFI";

/* ---------- GET / ---------- */
esp_err_t web_wifi_page_handler(httpd_req_t *req)
{
    size_t template_len = strlen(wifi_config_html);
    size_t buf_size = template_len+128; // 预留足够空间
    char *buf = malloc(buf_size);
    
    if (buf == NULL) return ESP_FAIL;

    // 执行转换
    int result = snprintf(buf, buf_size, wifi_config_html, FW_VERSION, HW_VERSION);

    // ⭐ 打印关键调试信息
    ESP_LOGI("DEBUG", "模板长度: %d, 生成长度: %d, 缓冲区大小: %d", template_len, result, buf_size);
    
    // 如果 result 小于 template_len，说明 snprintf 在中途就停止了
    // 如果 result 很大但页面只显示一部分，说明中间有隐形 \0

    httpd_resp_set_type(req, "text/html");
    esp_err_t ret = httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
    free(buf);
    return ret;
}

/* ---------- POST /wifi ---------- */
esp_err_t wifi_post_handler(httpd_req_t *req)
{
    char buf[256];

    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "recv failed");
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    char ssid[33] = {0};
    char password[65] = {0};

    httpd_query_key_value(buf, "ssid", ssid, sizeof(ssid));
    httpd_query_key_value(buf, "password", password, sizeof(password));

    ESP_LOGI(TAG, "Apply WiFi ssid=%s", ssid);

    wifi_apply_config(ssid, password);

    httpd_resp_sendstr(req, "OK, connecting...");
    return ESP_OK;
}
