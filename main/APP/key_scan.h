/**
 ****************************************************************************************************
 * @file        
 ****************************************************************************************************
 * @attention
 ****************************************************************************************************
 */

#ifndef __KEY_SCAN_H
#define __KEY_SCAN_H

void key_scan_task(void *arg);
void ota_daemon_task(void *pvParameter);
void lcd_show_homepage(const char *ssid, const char *ip_str, bool is_config_mode);
extern esp_mqtt_client_handle_t client;
#endif
