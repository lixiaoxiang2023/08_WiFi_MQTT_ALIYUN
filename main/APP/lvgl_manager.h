#ifndef __UI_H__
#define __UI_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
typedef enum {
    LVGL_MSG_SET_STATUS = 1,
    LVGL_MSG_SET_SSID,
    LVGL_MSG_SET_IP,
    LVGL_MSG_SET_MODE,   // ❗这个没处理
    LVGL_MSG_SET_OTA_TEXT,
    LVGL_MSG_SET_PROGRESS,
    LVGL_MSG_DOWNLOAD_PROGRESS
} lvgl_msg_type_t;

typedef struct {
    lvgl_msg_type_t type;
    char text[64];
} lvgl_msg_t;

typedef struct {
    int percentage;
    float speed_kb;
    int eta_min;
    int eta_sec;
    char status[32];
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
void ui_set_ota(const char *text);

/* =========================
 * LVGL消息队列（外部创建）
 * ========================= */
extern QueueHandle_t lvgl_queue;

#ifdef __cplusplus
}
#endif

#endif /* __UI_H__ */