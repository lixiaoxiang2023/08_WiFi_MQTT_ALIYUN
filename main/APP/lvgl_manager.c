#include "lvgl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "lvgl_manager.h"
#include "esp_log.h"

extern QueueHandle_t lvgl_queue;

// UI对象（全局）
static lv_obj_t *label_status;
static lv_obj_t *label_ssid;
static lv_obj_t *label_ip;

static void lvgl_send(lvgl_msg_type_t type, const char *text)
{
    lvgl_msg_t msg = {0};
    msg.type = type;

    if (text) {
        strncpy(msg.text, text, sizeof(msg.text) - 1);
    }

    xQueueSend(lvgl_queue, &msg, 0);
}

void ui_create(void)
{
    lv_obj_t *scr = lv_scr_act();
    static lv_obj_t *label_progress;
    label_progress = lv_label_create(scr);
    lv_obj_align(label_progress, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_label_set_text(label_progress, "0% 0KB/s");

    label_status = lv_label_create(scr);
    lv_obj_align(label_status, LV_ALIGN_TOP_MID, 0, 20);
    lv_label_set_text(label_status, "SYSTEM START");

    label_ssid = lv_label_create(scr);
    lv_obj_align(label_ssid, LV_ALIGN_CENTER, 0, -20);
    lv_label_set_text(label_ssid, "SSID: ---");

    label_ip = lv_label_create(scr);
    lv_obj_align(label_ip, LV_ALIGN_CENTER, 0, 20);
    lv_label_set_text(label_ip, "IP: ---");
}

void ui_set_progress(int percent, int speed_kb, int eta_min, int eta_sec)
{
    char buf[64];
    snprintf(buf, sizeof(buf),
             "%d%% %dKB/s %02d:%02d",
             percent, speed_kb, eta_min, eta_sec);

    lvgl_send(LVGL_MSG_SET_PROGRESS, buf);
}

void lvgl_task(void *arg)
{
    lvgl_msg_t msg;

    ui_create();

    while (1)
    {
        // 1. 处理UI消息
        while (xQueueReceive(lvgl_queue, &msg, 0))
        {
            switch (msg.type)
            {
                case LVGL_MSG_SET_STATUS:
                    lv_label_set_text(label_status, msg.text);
                    break;

                case LVGL_MSG_SET_SSID:
                    lv_label_set_text(label_ssid, msg.text);
                    break;

                case LVGL_MSG_SET_IP:
                    lv_label_set_text(label_ip, msg.text);
                    break;

                case LVGL_MSG_SET_OTA_TEXT:
                    lv_label_set_text(label_status, msg.text);
                    break;
                
                case LVGL_MSG_DOWNLOAD_PROGRESS:
                {
                    download_ui_msg_t *p = (download_ui_msg_t *)msg.text;

                    char buf[64];
                    snprintf(buf, sizeof(buf),
                            "%d%% %.1fKB/s %02d:%02d",
                            p->percentage,
                            p->speed_kb,
                            p->eta_min,
                            p->eta_sec);
                    ESP_LOGI("LVGL", "Status: %s", buf);
                    lv_label_set_text(label_status, buf);
                    break;
                }
                default:
                    break;
            }
        }

        // 2. LVGL刷新
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void ui_set_status(const char *text)
{
    lvgl_send(LVGL_MSG_SET_STATUS, text);
}

void ui_set_ssid(const char *text)
{
    lvgl_send(LVGL_MSG_SET_SSID, text);
}

void ui_set_ip(const char *text)
{
    lvgl_send(LVGL_MSG_SET_IP, text);
}

void ui_set_ota(const char *text)
{
    lvgl_send(LVGL_MSG_SET_OTA_TEXT, text);
}