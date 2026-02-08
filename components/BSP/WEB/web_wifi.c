#include "web_wifi.h"
#include "web_pages.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "../../main/APP/wifi_config.h"


static const char *TAG = "WEB_WIFI";

/* ---------- GET / ---------- */
esp_err_t web_wifi_page_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, wifi_config_html, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
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
