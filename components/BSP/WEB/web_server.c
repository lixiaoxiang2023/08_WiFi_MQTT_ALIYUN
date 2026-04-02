#include "web_server.h"
#include "web_wifi.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "../../main/APP/wifi_config.h"
#include "web_server_handlers.h" // 包含新的处理器头文件
#include "obs_http.h" // 包含新的处理器头文件

static const char *TAG = "WEB_SERVER";
static httpd_handle_t server = NULL;

/* ---------- GET / ---------- */
static httpd_uri_t uri_root = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = web_wifi_page_handler
};

/* ---------- POST /wifi ---------- */
// 这个处理器通常用于 SmartConfig 后端的直接 POST，
// 如果现在所有 WiFi 连接都通过 /connect_wifi handler，这个可以移除或重新设计。
// 暂时保留，但注意其用途。
static httpd_uri_t uri_wifi = {
    .uri = "/wifi",
    .method = HTTP_POST,
    .handler = wifi_post_handler
};

void web_server_start(void)
{
    if (server) return;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 20; // 增加到足够多的数量，因为有很多新的处理器
    config.stack_size  = 10240;

    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_register_uri_handler(server, &uri_root);
        httpd_register_uri_handler(server, &uri_wifi); // 保留或根据需要移除
        ESP_LOGI(TAG, "Web server started");
    }

    // 注册所有 Web 相关的 URI 处理器 (从 wifi_config.c 移动过来，以及新的仪器配置处理器)
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
    // 新的仪器配置处理器
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
        httpd_uri_t get_products_uri = {
        .uri       = "/api/get_product_list", // 注意末尾的星号
        .method    = HTTP_GET,
        .handler   = get_product_list_handler,
        .user_ctx  = NULL
        };

        httpd_uri_t uri_get_platforms = {
        .uri      = "/api/get_platforms",
        .method   = HTTP_GET,
        .handler  = get_platforms_handler,
        .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &uri_get_platforms);

        httpd_uri_t uri_get_versions = {
            .uri      = "/api/get_versions",
            .method   = HTTP_GET,
            .handler  = get_versions_handler,
            .user_ctx = NULL
        };
    httpd_register_uri_handler(server, &uri_get_versions);
    httpd_register_uri_handler(server, &get_products_uri);
    httpd_register_uri_handler(server, &wifi_info_uri);
    httpd_register_uri_handler(server, &get_config_uri);
    httpd_register_uri_handler(server, &usb_files_uri);
    httpd_register_uri_handler(server, &wifi_scan_uri);  
    httpd_register_uri_handler(server, &save_config_uri);
    httpd_register_uri_handler(server, &connect_wifi_uri);
    // 注册新的仪器配置处理器
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

