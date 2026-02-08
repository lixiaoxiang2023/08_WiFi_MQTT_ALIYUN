#pragma once

#include "esp_http_server.h"

esp_err_t web_wifi_page_handler(httpd_req_t *req);
esp_err_t wifi_post_handler(httpd_req_t *req);
esp_err_t wifi_apply_config(const char *ssid, const char *password);
