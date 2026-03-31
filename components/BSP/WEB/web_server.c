#include "web_server.h"
#include "web_wifi.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "../../main/APP/wifi_config.h"
#include "web_server_handlers.h" // Include the new handlers header

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
    config.max_uri_handlers = 10; // Increased to accommodate new handlers
  //  config.stack_size  = 8096;

    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_register_uri_handler(server, &uri_root);
        httpd_register_uri_handler(server, &uri_wifi);
        ESP_LOGI(TAG, "Web server started");
    }
        httpd_uri_t wifi_scan_uri = {
        .uri      = "/scan",
        .method   = HTTP_GET,
        .handler  = wifi_scan_handler,
        .user_ctx = NULL
    };
    httpd_uri_t save_config_uri = {
        .uri      = "/save_config",
        .method   = HTTP_POST,
        .handler  = save_config_handler,
        .user_ctx = NULL
    };
    httpd_uri_t connect_wifi_uri = {
        .uri      = "/connect_wifi",
        .method   = HTTP_POST,
        .handler  = connect_wifi_handler,
        .user_ctx = NULL
    };
    httpd_uri_t usb_files_uri = {
        .uri      = "/usb_files",
        .method   = HTTP_GET,
        .handler  = usb_files_handler,
        .user_ctx = NULL
    };
    httpd_uri_t get_config_uri = {
        .uri = "/get_config",
        .method = HTTP_GET,
        .handler = get_config_handler,
        .user_ctx = NULL
    };
    httpd_uri_t wifi_info_uri = {
        .uri      = "/get_wifi_info",
        .method   = HTTP_GET,
        .handler  = get_wifi_info_handler,
        .user_ctx = NULL
    };
    // New handlers for instrument configuration
    httpd_uri_t get_instrument_config_uri = {
        .uri       = "/get_instrument_config",
        .method    = HTTP_GET,
        .handler   = get_instrument_config_handler,
        .user_ctx  = NULL
    };
    httpd_uri_t save_instrument_config_uri = {
        .uri       = "/save_instrument_config",
        .method    = HTTP_POST,
        .handler   = save_instrument_config_handler,
        .user_ctx  = NULL
    };


    httpd_register_uri_handler(server, &wifi_info_uri);
    httpd_register_uri_handler(server, &get_config_uri);
    httpd_register_uri_handler(server, &usb_files_uri);
    httpd_register_uri_handler(server, &wifi_scan_uri);  
    httpd_register_uri_handler(server, &save_config_uri);
    httpd_register_uri_handler(server, &connect_wifi_uri);
    // Register new instrument configuration handlers
    httpd_register_uri_handler(server, &get_instrument_config_uri);
    httpd_register_uri_handler(server, &save_instrument_config_uri);

}

void web_server_stop(void)
{
    if (server) {
        httpd_stop(server);
        server = NULL;
        ESP_LOGI(TAG, "Web server stopped");
    }
}

