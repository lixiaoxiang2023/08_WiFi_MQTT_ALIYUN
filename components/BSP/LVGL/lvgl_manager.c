#include "lvgl_manager.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

extern QueueHandle_t lvgl_queue;

/* Android 风格配色 */
#define MD_COLOR_BG       lv_color_hex(0xF0F4F9)
#define MD_COLOR_CARD     lv_color_hex(0xFFFFFF)
#define MD_COLOR_PRIMARY  lv_color_hex(0x005AC1)
#define MD_COLOR_TXT_MAIN lv_color_hex(0x1A1C1E)
#define MD_COLOR_TXT_SEC  lv_color_hex(0x42474E)
#define MD_COLOR_ACCENT   lv_color_hex(0xD3E3FD)

/* 状态结构体定义 - 修正至全局作用域 */
typedef struct {
    bool wifi_connected;
    bool usb_connected;
    bool apsta_mode;
    int download_percent;
    float download_speed_kb;
    int download_eta_min;
    int download_eta_sec;
    uint32_t download_total_bytes;
    uint32_t download_done_bytes;
    char status[96];
    char ssid[64];
    char ip[32];
    char web_ip[32];
    char download_file[64];
    char ota_status[96];
} ui_state_t;

static ui_state_t g_ui_state = { .web_ip = "192.168.4.1" };
static lv_obj_t *g_pages[UI_PAGE_MAX];
static lv_obj_t *g_time_label, *g_wifi_badge, *g_usb_badge;

/* 页面组件句柄 */
static lv_obj_t *g_boot_status, *g_boot_card;
static lv_obj_t *g_wifi_ssid_val, *g_wifi_ip_val, *g_wifi_web_val, *g_wifi_mode_val;
static lv_obj_t *g_dl_file_name, *g_dl_bar, *g_dl_percent, *g_dl_speed, *g_dl_status;

/* --- 原子组件设计 --- */
static lv_obj_t *ui_create_md_card(lv_obj_t *parent, lv_coord_t h) {
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, 300, h);
    lv_obj_set_style_radius(card, 24, 0); 
    lv_obj_set_style_bg_color(card, MD_COLOR_CARD, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0); // 彻底遮挡底层
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_shadow_width(card, 15, 0);
    lv_obj_set_style_shadow_opa(card, LV_OPA_10, 0);
    lv_obj_set_style_pad_all(card, 16, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

static lv_obj_t *ui_create_md_item(lv_obj_t *parent, const char *symbol, const char *name, int y) {
    lv_obj_t *icon = lv_label_create(parent);
    lv_label_set_text(icon, symbol);
    lv_obj_set_style_text_color(icon, MD_COLOR_PRIMARY, 0);
    lv_obj_set_pos(icon, 0, y + 6);

    lv_obj_t *t = lv_label_create(parent);
    lv_label_set_text(t, name);
    lv_obj_set_style_text_font(t, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(t, MD_COLOR_TXT_SEC, 0);
    lv_obj_set_pos(t, 28, y);

    lv_obj_t *val = lv_label_create(parent);
    lv_obj_set_width(val, 220);
    lv_label_set_long_mode(val, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(val, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(val, MD_COLOR_TXT_MAIN, 0);
    lv_obj_set_pos(val, 28, y + 18);
    return val;
}
void ui_show_home(void) {
    ui_show_page(UI_PAGE_HOME);
}

void ui_show_wifi(void) {
    ui_show_page(UI_PAGE_WIFI);
}

void ui_show_download(void) {
    ui_show_page(UI_PAGE_DOWNLOAD);
}
/* --- 页面初始化 --- */
static void ui_create_boot_page(void) {
    g_pages[UI_PAGE_BOOT] = lv_obj_create(lv_scr_act());
    lv_obj_set_size(g_pages[UI_PAGE_BOOT], 320, 198);
    lv_obj_align(g_pages[UI_PAGE_BOOT], LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(g_pages[UI_PAGE_BOOT], 0, 0);

    g_boot_card = ui_create_md_card(g_pages[UI_PAGE_BOOT], 180);
    lv_obj_align(g_boot_card, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t *sp = lv_spinner_create(g_boot_card, 1000, 60);
    lv_obj_set_size(sp, 55, 55);
    lv_obj_align(sp, LV_ALIGN_CENTER, 0, -20);
    lv_obj_set_style_arc_color(sp, MD_COLOR_PRIMARY, LV_PART_INDICATOR);

    g_boot_status = lv_label_create(g_boot_card);
    lv_obj_set_width(g_boot_status, 260);
    lv_obj_set_style_text_align(g_boot_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(g_boot_status, LV_ALIGN_BOTTOM_MID, 0, -5);
}
/* --- 修正后的 WiFi 页面创建 (修复 WEB IP 标题) --- */
static void ui_create_wifi_page(void) {
    g_pages[UI_PAGE_WIFI] = lv_obj_create(lv_scr_act());
    lv_obj_set_size(g_pages[UI_PAGE_WIFI], 320, 198);
    lv_obj_align(g_pages[UI_PAGE_WIFI], LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(g_pages[UI_PAGE_WIFI], LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *card = ui_create_md_card(g_pages[UI_PAGE_WIFI], 185);
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 0);

    // 基础信息列表
    g_wifi_ssid_val = ui_create_md_item(card, LV_SYMBOL_WIFI, "SSID", 5);
    g_wifi_ip_val   = ui_create_md_item(card, LV_SYMBOL_DIRECTORY, "IP Address", 50);
    g_wifi_mode_val = ui_create_md_item(card, LV_SYMBOL_SETTINGS, "Mode", 95);

    // --- 修复：WEB IP 标题显示 ---
    lv_obj_t *web_title = lv_label_create(card);
    lv_label_set_text(web_title, "Web Server");
    lv_obj_set_style_text_font(web_title, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(web_title, MD_COLOR_TXT_SEC, 0);
    lv_obj_set_pos(web_title, 160, 95); // 放在 Mode 右侧

    lv_obj_t *chip = lv_obj_create(card);
    lv_obj_set_size(chip, 130, 30);
    lv_obj_set_pos(chip, 145, 115); // 放在标题下方
    lv_obj_set_style_bg_color(chip, MD_COLOR_ACCENT, 0);
    lv_obj_set_style_radius(chip, 15, 0);
    lv_obj_set_style_border_width(chip, 0, 0);

    g_wifi_web_val = lv_label_create(chip);
    lv_obj_align(g_wifi_web_val, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_color(g_wifi_web_val, MD_COLOR_PRIMARY, 0);
}
/**
 * @brief 安全更新 Label 文字，先清空擦除，再显示新内容
 * @param obj Label 对象句柄
 * @param new_text 新的文字内容
 */
void ui_safe_set_label_text(lv_obj_t *obj, const char *new_text) {
    if (obj == NULL) return;

    // 1. 设置为空字符串
    lv_label_set_text(obj, "");
    
    // 2. 强制该对象区域立即失效（标记为需要重新用背景色涂抹）
    lv_obj_invalidate(obj);
    
    // 3. 强制 LVGL 立即执行重绘逻辑，而不是等待下一帧
    // 这步非常关键，它会强制 SPI 立即发出“擦除”包
    lv_refr_now(NULL); 
    
    // 4. (可选) 给 SPI 传输留出微小的物理时间，确保擦除帧已发完
    // vTaskDelay(pdMS_TO_TICKS(5)); 

    // 5. 设置真正的内容
    lv_label_set_text(obj, new_text);
    lv_obj_invalidate(obj);
}
/* --- 修正后的刷新逻辑 (彻底解决重影) --- */
static void ui_refresh(void) {
    char buf[64];
    
    if (g_time_label) lv_obj_invalidate(g_time_label);
    if (g_boot_status) lv_obj_invalidate(g_boot_status);
    // 1. 刷新状态栏（全局唯一，不应有重影）
    time_t now = time(NULL);
    struct tm tinfo;
    if (localtime_r(&now, &tinfo)) {
        strftime(buf, sizeof(buf), "%H:%M", &tinfo);
        // 如果这里有重影，说明之前可能创建了两个状态栏，请检查 ui_create 是否被调用了两次
        lv_label_set_text(g_time_label, buf); 
    }
    lv_obj_set_style_text_color(g_wifi_badge, g_ui_state.wifi_connected ? MD_COLOR_PRIMARY : MD_COLOR_TXT_SEC, 0);
    lv_obj_set_style_text_color(g_usb_badge, g_ui_state.usb_connected ? lv_color_hex(0x21A365) : MD_COLOR_TXT_SEC, 0);

    // 2. 页面互斥刷新逻辑：非当前页面内容强制清空
    // 刷新 Boot 页
    if (!lv_obj_has_flag(g_pages[UI_PAGE_BOOT], LV_OBJ_FLAG_HIDDEN)) {
        lv_label_set_text(g_boot_status, g_ui_state.status);
    } else {
        lv_label_set_text(g_boot_status, ""); // 切走时清空，防止像素残留
    }

    // 刷新 Wifi 页
    if (!lv_obj_has_flag(g_pages[UI_PAGE_WIFI], LV_OBJ_FLAG_HIDDEN)) {
        lv_label_set_text(g_wifi_ssid_val, g_ui_state.ssid);
        lv_label_set_text(g_wifi_ip_val, g_ui_state.ip);
        lv_label_set_text(g_wifi_mode_val, g_ui_state.apsta_mode ? "AP + STA" : "STA");
        lv_label_set_text(g_wifi_web_val, g_ui_state.web_ip);
    } else {
        // 如果不在 Wifi 页，清空这些 Label，确保它们不会穿透
        lv_label_set_text(g_wifi_ssid_val, "");
        lv_label_set_text(g_wifi_ip_val, "");
    }

    // 刷新 Download 页
    if (!lv_obj_has_flag(g_pages[UI_PAGE_DOWNLOAD], LV_OBJ_FLAG_HIDDEN)) {
        lv_label_set_text(g_dl_file_name, g_ui_state.download_file);
        lv_bar_set_value(g_dl_bar, g_ui_state.download_percent, LV_ANIM_ON);
        snprintf(buf, sizeof(buf), "%d%%", g_ui_state.download_percent);
        lv_label_set_text(g_dl_percent, buf);
        snprintf(buf, sizeof(buf), "%.1f KB/s", g_ui_state.download_speed_kb);
        lv_label_set_text(g_dl_speed, buf);
        lv_label_set_text(g_dl_status, g_ui_state.ota_status);
    } else {
        lv_label_set_text(g_dl_status, "");
    }
}
static void ui_create_download_page(void) {
    g_pages[UI_PAGE_DOWNLOAD] = lv_obj_create(lv_scr_act());
    lv_obj_set_size(g_pages[UI_PAGE_DOWNLOAD], 320, 198);
    lv_obj_align(g_pages[UI_PAGE_DOWNLOAD], LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(g_pages[UI_PAGE_DOWNLOAD], LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *card = ui_create_md_card(g_pages[UI_PAGE_DOWNLOAD], 185);
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 0);
    // 关键：减小左右间距，给文字腾空间
    lv_obj_set_style_pad_left(card, 12, 0);
    lv_obj_set_style_pad_right(card, 12, 0);

    g_dl_file_name = ui_create_md_item(card, LV_SYMBOL_DOWNLOAD, "Updating System", 5);
    
    // 进度条稍微调短一点，给两边留白
    g_dl_bar = lv_bar_create(card);
    lv_obj_set_size(g_dl_bar, 250, 10);
    lv_obj_set_pos(g_dl_bar, 10, 65);
    lv_obj_set_style_bg_color(g_dl_bar, MD_COLOR_ACCENT, LV_PART_MAIN);
    lv_obj_set_style_bg_color(g_dl_bar, MD_COLOR_PRIMARY, LV_PART_INDICATOR);

    // 百分比：靠左对齐
    g_dl_percent = lv_label_create(card);
    lv_obj_set_style_text_font(g_dl_percent, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(g_dl_percent, 10, 80);

    // 速度：靠右对齐，且设置最大宽度防止超框
    g_dl_speed = lv_label_create(card);
    lv_obj_set_style_text_font(g_dl_speed, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(g_dl_speed, MD_COLOR_TXT_SEC, 0);
    lv_obj_set_width(g_dl_speed, 120); 
    lv_obj_set_style_text_align(g_dl_speed, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(g_dl_speed, LV_ALIGN_TOP_RIGHT, -10, 80);

    // 状态描述
    g_dl_status = lv_label_create(card);
    lv_obj_set_width(g_dl_status, 260);
    lv_obj_set_style_text_font(g_dl_status, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(g_dl_status, MD_COLOR_PRIMARY, 0);
    lv_obj_align(g_dl_status, LV_ALIGN_BOTTOM_LEFT, 10, -5);
}

static void ui_switch_page_inner(ui_page_t page) {
    for (int i = 0; i < UI_PAGE_MAX; i++) {
        if (g_pages[i]) {
            if (i == (int)page) {
                lv_obj_clear_flag(g_pages[i], LV_OBJ_FLAG_HIDDEN);
                lv_obj_move_foreground(g_pages[i]);
            } else {
                lv_obj_add_flag(g_pages[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
}

/* --- 业务端封装接口实现 --- */
void ui_set_status(const char *t) {
    lvgl_msg_t m = { .type = LVGL_MSG_SET_STATUS };
    if(t) strlcpy(m.text, t, 96);
    xQueueSend(lvgl_queue, &m, 0);
}
void ui_set_ssid(const char *t) {
    lvgl_msg_t m = { .type = LVGL_MSG_SET_SSID };
    if(t) strlcpy(m.text, t, 64);
    xQueueSend(lvgl_queue, &m, 0);
}
void ui_set_ip(const char *t) {
    lvgl_msg_t m = { .type = LVGL_MSG_SET_IP };
    if(t) strlcpy(m.text, t, 32);
    xQueueSend(lvgl_queue, &m, 0);
}
void ui_set_web_ip(const char *t) {
    lvgl_msg_t m = { .type = LVGL_MSG_SET_WEB_IP };
    if(t) strlcpy(m.text, t, 32);
    xQueueSend(lvgl_queue, &m, 0);
}
void ui_set_ota(const char *t) {
    lvgl_msg_t m = { .type = LVGL_MSG_SET_OTA_TEXT };
    if(t) strlcpy(m.text, t, 96);
    xQueueSend(lvgl_queue, &m, 0);
}
void ui_set_wifi_mode(bool mode) {
    lvgl_msg_t m = { .type = LVGL_MSG_SET_WIFI_MODE, .value = mode };
    xQueueSend(lvgl_queue, &m, 0);
}
void ui_set_wifi_connected(bool c) {
    lvgl_msg_t m = { .type = LVGL_MSG_SET_WIFI_CONNECTED, .value = c };
    xQueueSend(lvgl_queue, &m, 0);
}
void ui_set_usb_connected(bool c) {
    lvgl_msg_t m = { .type = LVGL_MSG_SET_USB_CONNECTED, .value = c };
    xQueueSend(lvgl_queue, &m, 0);
}
void ui_show_page(ui_page_t p) {
    lvgl_msg_t m = { .type = LVGL_MSG_SHOW_PAGE, .value = p };
    xQueueSend(lvgl_queue, &m, 0);
}
void ui_set_download_info(const char *fn, uint32_t size) {
    lvgl_msg_t m = { .type = LVGL_MSG_SET_DOWNLOAD_META, .value = (int32_t)size };
    if(fn) strlcpy(m.text, fn, 64);
    xQueueSend(lvgl_queue, &m, 0);
}
void ui_push_download(int p, float s, int em, int es, uint32_t db, uint32_t tb) {
    lvgl_msg_t m = { .type = LVGL_MSG_DOWNLOAD_PROGRESS };
    download_ui_msg_t payload = {p, s, em, es, db, tb};
    memcpy(m.text, &payload, sizeof(payload));
    xQueueSend(lvgl_queue, &m, 0);
}

void ui_create(void) {
    lv_obj_t *scr = lv_scr_act();
    
    // 【关键补丁 1】强制屏幕背景不透明，并设置为安卓灰白底色
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(scr, MD_COLOR_BG, 0);

    // 【关键补丁 2】状态栏容器也要设置背景色，确保它刷新时能“盖掉”之前的字符
    lv_obj_t *sb = lv_obj_create(scr);
    lv_obj_set_size(sb, 320, 36);
    lv_obj_align(sb, LV_ALIGN_TOP_MID, 0, 0);
    
    // 强制状态栏背景与屏幕一致且不透明
    lv_obj_set_style_bg_color(sb, MD_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(sb, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sb, 0, 0);
    lv_obj_set_style_radius(sb, 0, 0);
    lv_obj_clear_flag(sb, LV_OBJ_FLAG_SCROLLABLE);

    // 时间 Label
    g_time_label = lv_label_create(sb);
    lv_obj_set_style_text_font(g_time_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(g_time_label, MD_COLOR_TXT_MAIN, 0);
    lv_obj_align(g_time_label, LV_ALIGN_LEFT_MID, 12, 0);

    // USB 图标
    g_usb_badge = lv_label_create(sb);
    lv_label_set_text(g_usb_badge, LV_SYMBOL_USB);
    lv_obj_align(g_usb_badge, LV_ALIGN_RIGHT_MID, -12, 0);

    // WiFi 图标
    g_wifi_badge = lv_label_create(sb);
    lv_label_set_text(g_wifi_badge, LV_SYMBOL_WIFI);
    lv_obj_align_to(g_wifi_badge, g_usb_badge, LV_ALIGN_OUT_LEFT_MID, -8, 0);

    // 初始化页面
    ui_create_boot_page();
    ui_create_wifi_page();
    ui_create_download_page();
    
    ui_switch_page_inner(UI_PAGE_BOOT);
}

void lvgl_task(void *arg) {
    lvgl_msg_t msg;
    ui_create();
    while (1) {
        if (xQueueReceive(lvgl_queue, &msg, pdMS_TO_TICKS(10))) {
            switch (msg.type) {
                case LVGL_MSG_SET_STATUS: strlcpy(g_ui_state.status, msg.text, 96); break;
                case LVGL_MSG_SET_SSID:   strlcpy(g_ui_state.ssid, msg.text, 64); break;
                case LVGL_MSG_SET_IP:     strlcpy(g_ui_state.ip, msg.text, 32); break;
                case LVGL_MSG_SET_WEB_IP: strlcpy(g_ui_state.web_ip, msg.text, 32); break;
                case LVGL_MSG_SET_OTA_TEXT: strlcpy(g_ui_state.ota_status, msg.text, 96); break;
                case LVGL_MSG_SET_WIFI_MODE: g_ui_state.apsta_mode = msg.value; break;
                case LVGL_MSG_SET_WIFI_CONNECTED: g_ui_state.wifi_connected = msg.value; break;
                case LVGL_MSG_SET_USB_CONNECTED:  g_ui_state.usb_connected = msg.value; break;
                case LVGL_MSG_SHOW_PAGE:  ui_switch_page_inner((ui_page_t)msg.value); break;
                case LVGL_MSG_SET_DOWNLOAD_META:  strlcpy(g_ui_state.download_file, msg.text, 64); break;
                case LVGL_MSG_DOWNLOAD_PROGRESS: {
                    download_ui_msg_t *p = (download_ui_msg_t*)msg.text;
                    g_ui_state.download_percent = p->percentage;
                    g_ui_state.download_speed_kb = p->speed_kb;
                    break;
                }
            }
            ui_refresh();
        }
        lv_timer_handler();
    }
}