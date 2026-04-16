#include "ui_lvgl.h"

static lv_obj_t *label_title;
static lv_obj_t *label_status;
static lv_obj_t *label_ssid;
static lv_obj_t *label_ip;
static lv_obj_t *label_ver;
static lv_obj_t *label_center;


void ui_home_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_scr_load(scr);

    // 顶部栏
    lv_obj_t *top = lv_obj_create(scr);
    lv_obj_set_size(top, 320, 45);
    lv_obj_set_style_bg_color(top, lv_color_hex(0x003366), 0);
    lv_obj_set_style_border_width(top, 0, 0);

    label_title = lv_label_create(top);
    lv_label_set_text(label_title, "SYSTEM MONITOR");
    lv_obj_center(label_title);
    lv_obj_set_style_text_color(label_title, lv_color_white(), 0);

    // 卡片
    lv_obj_t *card = lv_obj_create(scr);
    lv_obj_set_size(card, 290, 120);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 10);
    lv_obj_set_style_bg_color(card, lv_color_white(), 0);

    lv_obj_t *l1 = lv_label_create(card);
    lv_label_set_text(l1, "Status:");
    lv_obj_align(l1, LV_ALIGN_TOP_LEFT, 10, 10);

    label_status = lv_label_create(card);
    lv_label_set_text(label_status, "STA ONLY");
    lv_obj_align(label_status, LV_ALIGN_TOP_LEFT, 90, 10);

    lv_obj_t *l2 = lv_label_create(card);
    lv_label_set_text(l2, "SSID:");
    lv_obj_align(l2, LV_ALIGN_TOP_LEFT, 10, 40);

    label_ssid = lv_label_create(card);
    lv_label_set_text(label_ssid, "----");
    lv_obj_align(label_ssid, LV_ALIGN_TOP_LEFT, 90, 40);

    lv_obj_t *l3 = lv_label_create(card);
    lv_label_set_text(l3, "IP:");
    lv_obj_align(l3, LV_ALIGN_TOP_LEFT, 10, 70);

    label_ip = lv_label_create(card);
    lv_label_set_text(label_ip, "Waiting...");
    lv_obj_align(label_ip, LV_ALIGN_TOP_LEFT, 90, 70);

    // bottom
    label_ver = lv_label_create(scr);
    lv_label_set_text(label_ver, "FW: V1.0");
    lv_obj_align(label_ver, LV_ALIGN_BOTTOM_MID, 0, -5);
}

void ui_home_update(const char *ssid, const char *ip, int mode)
{
    if (ssid)
        lv_label_set_text(label_ssid, ssid);

    if (ip)
        lv_label_set_text(label_ip, ip);

    if (mode == 1)
        lv_label_set_text(label_status, "STA+AP CONFIG");
    else
        lv_label_set_text(label_status, "STA ONLY");
}

void ui_init_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_scr_load(scr);

    label_center = lv_label_create(scr);
    lv_label_set_text(label_center, "SYSTEM INITIALIZING...");
    lv_obj_center(label_center);
}

void ui_init_update(const char *msg, lv_color_t color)
{
    if (!label_center) return;

    lv_label_set_text(label_center, msg);
    lv_obj_set_style_text_color(label_center, color, 0);
}

void ui_ota_update(const char *msg)
{
    ui_init_update(msg, lv_color_make(0, 255, 0));
}