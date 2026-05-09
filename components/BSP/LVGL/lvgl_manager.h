#ifndef LVGL_MANAGER_H
#define LVGL_MANAGER_H

#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>

/* 页面枚举 */
typedef enum {
    UI_PAGE_BOOT = 0,
    UI_PAGE_HOME,
    UI_PAGE_WIFI,
    UI_PAGE_DOWNLOAD,
    UI_PAGE_MAX
} ui_page_t;

/* 消息类型定义 */
typedef enum {
    LVGL_MSG_SET_STATUS,
    LVGL_MSG_SET_SSID,
    LVGL_MSG_SET_IP,
    LVGL_MSG_SET_WEB_IP,
    LVGL_MSG_SET_OTA_TEXT,
    LVGL_MSG_SET_WIFI_MODE,
    LVGL_MSG_SET_WIFI_CONNECTED,
    LVGL_MSG_SET_USB_CONNECTED,
    LVGL_MSG_SHOW_PAGE,
    LVGL_MSG_SET_DOWNLOAD_META,
    LVGL_MSG_DOWNLOAD_PROGRESS
} lvgl_msg_type_t;

/* 下载进度负载 */
typedef struct {
    int percentage;
    float speed_kb;
    int eta_min;
    int eta_sec;
    uint32_t downloaded_bytes;
    uint32_t total_bytes;
} download_ui_msg_t;

/* 消息结构体 */
typedef struct {
    lvgl_msg_type_t type;
    int32_t value;
    char text[96];
} lvgl_msg_t;

/* 业务层接口函数 - 保持原有命名 */
void ui_create(void);
void lvgl_task(void *arg);
void ui_show_page(ui_page_t page);
void ui_set_status(const char *text);
void ui_set_ssid(const char *text);
void ui_set_ip(const char *text);
void ui_set_web_ip(const char *text);
void ui_set_ota(const char *text);
void ui_set_wifi_mode(bool is_apsta_mode);
void ui_set_wifi_connected(bool connected);
void ui_set_usb_connected(bool connected);
void ui_set_download_info(const char *file_name, uint32_t total_bytes);
void ui_push_download(int percent, float speed_kb, int eta_min, int eta_sec, uint32_t downloaded_bytes, uint32_t total_bytes);
// 补充这三个缺失的便捷接口
void ui_show_home(void);
void ui_show_wifi(void);
void ui_show_download(void);
#endif