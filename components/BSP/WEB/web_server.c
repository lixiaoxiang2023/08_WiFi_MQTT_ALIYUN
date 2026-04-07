#include "web_server.h"
#include "web_wifi.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "../../main/APP/wifi_config.h"
#include "web_server_handlers.h" // 包含新的处理器头文件
#include "obs_http.h" // 包含新的处理器头文件

static const char *TAG = "WEB_SERVER";
static httpd_handle_t server = NULL;

/**
 * @brief URI handler for the root path "/".
 *        Serves the main Wi-Fi configuration page.
 */
static httpd_uri_t uri_root = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = web_wifi_page_handler
};

/**
 * @brief URI handler for POST requests to "/wifi".
 *        Historically used for SmartConfig backend POSTs.
 *        Consider removal or redesign if all Wi-Fi connections are handled via /connect_wifi.
 */
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
        httpd_register_uri_handler(server, &uri_wifi); // Retain or remove as needed
        ESP_LOGI(TAG, "Web server started");
    }

    // Register all Web-related URI handlers (moved from wifi_config.c and new instrument configuration handlers)

    // WiFi Configuration Handlers
    /**
     * @brief URI handler for GET requests to "/scan".
     *        Initiates and returns results of a Wi-Fi scan.
     */
        static const httpd_uri_t wifi_scan_uri = {
        .uri      = "/scan",
        .method   = HTTP_GET,
        .handler  = wifi_scan_handler,
        .user_ctx = NULL
    };

    /**
     * @brief URI handler for POST requests to "/save_config".
     *        Saves general configuration settings.
     */
        static const httpd_uri_t save_config_uri = {
            .uri      = "/save_config",
            .method   = HTTP_POST,
            .handler  = save_config_handler,
            .user_ctx = NULL
        };

    /**
     * @brief URI handler for POST requests to "/connect_wifi".
     *        Connects to a specified Wi-Fi network.
     */
        static const httpd_uri_t connect_wifi_uri = {
            .uri      = "/connect_wifi",
            .method   = HTTP_POST,
            .handler  = connect_wifi_handler,
            .user_ctx = NULL
        };

    /**
     * @brief URI handler for GET requests to "/get_wifi_info".
     *        Retrieves current Wi-Fi connection information.
     */
    static const httpd_uri_t wifi_info_uri = {
        .uri      = "/get_wifi_info",
        .method   = HTTP_GET,
        .handler  = get_wifi_info_handler,
        .user_ctx = NULL
        };

    // System and File Management Handlers
    /**
     * @brief URI handler for GET requests to "/usb_files".
     *        Lists files available on USB storage.
     */
    static const httpd_uri_t usb_files_uri = {
        .uri      = "/usb_files",
        .method   = HTTP_GET,
        .handler  = usb_files_handler,
        .user_ctx = NULL
        };

    /**
     * @brief URI handler for GET requests to "/get_config".
     *        Retrieves general device configuration.
     */
    static const httpd_uri_t get_config_uri = {
        .uri = "/get_config",
        .method = HTTP_GET,
        .handler = get_config_handler,
            .user_ctx = NULL
        };

    // Instrument Configuration Handlers
    /**
     * @brief URI handler for GET requests to "/get_instrument_config".
     *        Retrieves instrument-specific configuration.
     */
        static const httpd_uri_t get_instrument_config_uri = {
            .uri       = "/get_instrument_config",
        .method    = HTTP_GET,
            .handler   = get_instrument_config_handler,
            .user_ctx  = NULL
        };

    /**
     * @brief URI handler for POST requests to "/save_instrument_config".
     *        Saves instrument-specific configuration.
     */
        static const httpd_uri_t save_instrument_config_uri = {
        .uri       = "/save_instrument_config",
        .method    = HTTP_POST,
        .handler   = save_instrument_config_handler,
        .user_ctx  = NULL
        };

    // Product and Platform Information Handlers
    /**
     * @brief URI handler for GET requests to "/api/get_product_list".
     *        Retrieves a list of available products.
     */
        static const httpd_uri_t get_products_uri = {
        .uri       = "/api/get_product_list",
        .method    = HTTP_GET,
        .handler   = get_product_list_handler,
        .user_ctx  = NULL
        };

    /**
     * @brief URI handler for GET requests to "/api/get_platforms".
     *        Retrieves a list of supported platforms.
     */
        static const httpd_uri_t uri_get_platforms = {
        .uri      = "/api/get_platforms",
        .method   = HTTP_GET,
        .handler  = get_platforms_handler,
        .user_ctx = NULL
        };

    /**
     * @brief URI handler for GET requests to "/api/get_versions".
     *        Retrieves software version information.
     */
        static const httpd_uri_t uri_get_versions = {
            .uri      = "/api/get_versions",
            .method   = HTTP_GET,
            .handler  = get_versions_handler,
            .user_ctx = NULL
        };

    // Firmware Update Handler
    /**
     * @brief URI handler for POST requests to "/do_update".
     *        Initiates a firmware update process.
     */
        static const httpd_uri_t update_uri = {
            .uri       = "/do_update",
            .method    = HTTP_POST,
            .handler   = update_handler,
            .user_ctx  = NULL
        };

    // Register all URI handlers with the web server
    httpd_register_uri_handler(server, &wifi_scan_uri);
    httpd_register_uri_handler(server, &save_config_uri);
    httpd_register_uri_handler(server, &connect_wifi_uri);
    httpd_register_uri_handler(server, &usb_files_uri);
    httpd_register_uri_handler(server, &get_config_uri);
    httpd_register_uri_handler(server, &wifi_info_uri);
    httpd_register_uri_handler(server, &get_instrument_config_uri);
    httpd_register_uri_handler(server, &save_instrument_config_uri);
    httpd_register_uri_handler(server, &get_products_uri);
    httpd_register_uri_handler(server, &uri_get_platforms);
    httpd_register_uri_handler(server, &uri_get_versions);
    httpd_register_uri_handler(server, &update_uri);
}

void web_server_stop(void)
{
    if (server) {
        httpd_stop(server);
        server = NULL;
        ESP_LOGI(TAG, "Web server stopped");
    }
}

