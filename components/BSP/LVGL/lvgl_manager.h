#ifndef __UI_H__
#define __UI_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
/* 信号量/互斥锁头文件 */
#include "freertos/semphr.h"

typedef enum {
    UI_PAGE_BOOT = 0,
    UI_PAGE_HOME,
    UI_PAGE_WIFI,
    UI_PAGE_DOWNLOAD
} ui_page_t;

typedef enum {
    LVGL_MSG_SET_STATUS = 1,
    LVGL_MSG_SET_SSID,
    LVGL_MSG_SET_IP,
    LVGL_MSG_SET_WEB_IP,
    LVGL_MSG_SET_MODE,   // ❗这个没处理
    LVGL_MSG_SET_OTA_TEXT,
    LVGL_MSG_SET_PROGRESS,
    LVGL_MSG_DOWNLOAD_PROGRESS,
    LVGL_MSG_SET_WIFI_MODE,
    LVGL_MSG_SET_WIFI_CONNECTED,
    LVGL_MSG_SET_USB_CONNECTED,
    LVGL_MSG_SHOW_PAGE,
    LVGL_MSG_SET_DOWNLOAD_META
} lvgl_msg_type_t;

typedef struct {
    lvgl_msg_type_t type;
    int32_t value;
    char text[96];
} lvgl_msg_t;

typedef struct {
    int percentage;
    float speed_kb;
    int eta_min;
    int eta_sec;
    uint32_t downloaded_bytes;
    uint32_t total_bytes;
} download_ui_msg_t;

/* =========================
 * UI任务
 * ========================= */
void lvgl_task(void *arg);

/* =========================
 * UI初始化
 * ========================= */
void ui_create(void);

/* =========================
 * UI对外接口（线程安全，通过队列）
 * ========================= */
void ui_set_status(const char *text);
void ui_set_ssid(const char *text);
void ui_set_ip(const char *text);
void ui_set_web_ip(const char *text);
void ui_set_ota(const char *text);
void ui_set_wifi_mode(bool is_apsta_mode);
void ui_set_wifi_connected(bool connected);
void ui_set_usb_connected(bool connected);
void ui_show_page(ui_page_t page);
void ui_show_home(void);
void ui_show_wifi(void);
void ui_show_download(void);
void ui_set_download_info(const char *file_name, uint32_t total_bytes);
void ui_push_download(int percent,
                      float speed_kb,
                      int eta_min,
                      int eta_sec,
                      uint32_t downloaded_bytes,
                      uint32_t total_bytes);
/* =========================
 * LVGL消息队列（外部创建）
 * ========================= */
extern QueueHandle_t lvgl_queue;
extern SemaphoreHandle_t global_spi2_mux;
#ifdef __cplusplus
}
#endif

#endif /* __UI_H__ */
