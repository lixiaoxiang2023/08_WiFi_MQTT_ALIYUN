#include "web_server.h"
#include "web_wifi.h"
#include "esp_http_server.h"
#include "esp_log.h"

static const char *TAG = "WEB_SERVER";
static httpd_handle_t server = NULL;

/* ---------- GET / ---------- */
static httpd_uri_t uri_root = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = web_wifi_page_handler
};

/* ---------- POST /wifi ---------- */
static httpd_uri_t uri_wifi = {
    .uri = "/wifi",
    .method = HTTP_POST,
    .handler = wifi_post_handler
};

void web_server_start(void)
{
    if (server) return;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 8;

    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_register_uri_handler(server, &uri_root);
        httpd_register_uri_handler(server, &uri_wifi);
        ESP_LOGI(TAG, "Web server started");
    }
}

void web_server_stop(void)
{
    if (server) {
        httpd_stop(server);
        server = NULL;
        ESP_LOGI(TAG, "Web server stopped");
    }
}
