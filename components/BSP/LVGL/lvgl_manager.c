#include <stdio.h>
#include <string.h>
#include <time.h>
#include "lvgl_manager.h"

extern QueueHandle_t lvgl_queue;

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

static const char *TAG = "LVGL_UI";
static ui_state_t g_ui_state = {
    .status = "Power-on initialization...",
    .ssid = "--",
    .ip = "--",
    .web_ip = "192.168.4.1",
    .download_file = "--",
    .ota_status = "Waiting for task"
};

static lv_obj_t *g_pages[4];
static lv_obj_t *g_time_label;
static lv_obj_t *g_wifi_badge;
static lv_obj_t *g_usb_badge;
static lv_obj_t *g_boot_status;
static lv_obj_t *g_home_status;
static lv_obj_t *g_home_mode;
static lv_obj_t *g_home_ssid;
static lv_obj_t *g_home_ip;
static lv_obj_t *g_home_hint;
static lv_obj_t *g_wifi_mode_value;
static lv_obj_t *g_wifi_ssid_value;
static lv_obj_t *g_wifi_connect_value;
static lv_obj_t *g_wifi_ip_value;
static lv_obj_t *g_wifi_web_ip_value;
static lv_obj_t *g_download_file_value;
static lv_obj_t *g_download_size_value;
static lv_obj_t *g_download_speed_value;
static lv_obj_t *g_download_eta_value;
static lv_obj_t *g_download_percent_value;
static lv_obj_t *g_download_status_value;
static lv_obj_t *g_download_bar;

static void lvgl_send_text(lvgl_msg_type_t type, const char *text)
{
    lvgl_msg_t msg = {0};
    msg.type = type;
    if (text) {
        strncpy(msg.text, text, sizeof(msg.text) - 1);
    }
    xQueueSend(lvgl_queue, &msg, 0);
}

static void lvgl_send_value(lvgl_msg_type_t type, int32_t value)
{
    lvgl_msg_t msg = {0};
    msg.type = type;
    msg.value = value;
    xQueueSend(lvgl_queue, &msg, 0);
}

static const char *ui_mode_text(void)
{
    return g_ui_state.apsta_mode ? "AP + STA" : "STA Only";
}

static const char *ui_connect_text(void)
{
    return g_ui_state.wifi_connected ? "Connected" : "Disconnected";
}

static void ui_format_size(uint32_t bytes, char *buf, size_t len)
{
    if (bytes >= 1024U * 1024U) {
        snprintf(buf, len, "%.2f MB", (double)bytes / (1024.0 * 1024.0));
    } else if (bytes >= 1024U) {
        snprintf(buf, len, "%.1f KB", (double)bytes / 1024.0);
    } else {
        snprintf(buf, len, "%u B", (unsigned)bytes);
    }
}

static void ui_set_badge_state(lv_obj_t *badge, bool active, lv_color_t active_color)
{
    lv_color_t bg = active ? active_color : lv_color_hex(0xD0D7E2);
    lv_color_t text = active ? lv_color_white() : lv_color_hex(0x5B667A);
    lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(badge, bg, 0);
    lv_obj_set_style_text_color(badge, text, 0);
}

static lv_obj_t *ui_create_page(void)
{
    lv_obj_t *page = lv_obj_create(lv_scr_act());
    lv_obj_set_size(page, 320, 198);
    lv_obj_align(page, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(page, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_set_style_pad_all(page, 12, 0);
    lv_obj_set_scrollbar_mode(page, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    return page;
}

static lv_obj_t *ui_create_card(lv_obj_t *parent, lv_coord_t w, lv_coord_t h)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, w, h);
    lv_obj_set_style_radius(card, 20, 0);
// 【修改】确保背景 100% 遮盖，完全不透明
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0); 
    lv_obj_set_style_bg_color(card, lv_color_white(), 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_shadow_width(card, 16, 0);
    lv_obj_set_style_shadow_color(card, lv_color_hex(0xD1D9E8), 0);
    lv_obj_set_style_shadow_opa(card, LV_OPA_30, 0);
    lv_obj_set_style_pad_all(card, 14, 0);
// 关键：启用 Flex 布局（ROW_WRAP 表示水平排队，不够宽就自动换行）
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    
    // 设置组件之间的水平和垂直间距
    lv_obj_set_style_pad_gap(card, 12, 0); 
    
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}
static lv_obj_t *ui_create_field(lv_obj_t *parent, const char *title, lv_coord_t x, lv_coord_t y, lv_coord_t w)
{
    // 1. 创建一个小容器包裹标题和数值，定死它的位置和宽度
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_pos(cont, x, y);                // 使用传入的坐标
    lv_obj_set_size(cont, w, LV_SIZE_CONTENT); // 高度自适应
    lv_obj_set_style_bg_opa(cont, 0, 0);       // 透明背景
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 0, 0);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    
    // 2. 内部使用 Flex 垂直排列，让标题和数值上下对齐
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN); 
    lv_obj_set_style_pad_gap(cont, 2, 0); 

    lv_obj_t *label = lv_label_create(cont);
    lv_label_set_text(label, title);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0x7B8699), 0);

    lv_obj_t *value = lv_label_create(cont);
    lv_obj_set_width(value, lv_pct(100)); 
    // 关键：为了整洁，WiFi页面建议用 DOT（超出显示省略号），下载页面才用 SCROLL
    lv_label_set_long_mode(value, LV_LABEL_LONG_DOT); 
    lv_obj_set_style_text_font(value, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(value, lv_color_hex(0x182033), 0);
    
    return value; 
}

static void ui_update_time(void)
{
    time_t now = time(NULL);
    struct tm timeinfo;
    char buf[16];

    if (localtime_r(&now, &timeinfo)) {
        strftime(buf, sizeof(buf), "%H:%M", &timeinfo);
    } else {
        snprintf(buf, sizeof(buf), "--:--");
    }

    if (g_time_label) {
        lv_label_set_text(g_time_label, buf);
    }
}

static void ui_switch_page_inner(ui_page_t page)
{
    for (int i = 0; i < 4; ++i) {
        if (!g_pages[i]) continue;
        
        if (i == (int)page) {
            lv_obj_clear_flag(g_pages[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(g_pages[i], LV_OBJ_FLAG_HIDDEN);
        }
    }

    // 【新增】如果切走的不是启动页，确保启动页内部的所有东西（即使是绝对定位的）也被彻底关掉
    if (page != UI_PAGE_BOOT) {
        if (g_pages[UI_PAGE_BOOT]) {
            lv_obj_add_flag(g_pages[UI_PAGE_BOOT], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void ui_refresh(void)
{
    char buf[128];
    char size_buf[24];
    char done_buf[24];
    char total_buf[24];

    // 1. 公共部分刷新（顶栏不受页面切换影响）
    ui_update_time();
    ui_set_badge_state(g_wifi_badge, g_ui_state.wifi_connected, lv_color_hex(0x268CFF));
    ui_set_badge_state(g_usb_badge, g_ui_state.usb_connected, lv_color_hex(0x21A365));

    // --- 核心逻辑：根据当前页面显示情况进行按需刷新 ---

    // 2. 刷新 BOOT 页面 (仅当 BOOT 页面未隐藏时)
    if (g_pages[UI_PAGE_BOOT] && !lv_obj_has_flag(g_pages[UI_PAGE_BOOT], LV_OBJ_FLAG_HIDDEN)) {
        lv_label_set_text(g_boot_status, g_ui_state.status);
        return; // 如果在启动页，刷完就退出，减少后续判断
    }

    // 3. 刷新 HOME 页面
    if (g_pages[UI_PAGE_HOME] && !lv_obj_has_flag(g_pages[UI_PAGE_HOME], LV_OBJ_FLAG_HIDDEN)) {
        lv_label_set_text(g_home_status, g_ui_state.status);
        snprintf(buf, sizeof(buf), "%s | %s", ui_mode_text(), ui_connect_text());
        lv_label_set_text(g_home_mode, buf);
        snprintf(buf, sizeof(buf), "SSID  %s", g_ui_state.ssid);
        lv_label_set_text(g_home_ssid, buf);
        snprintf(buf, sizeof(buf), "IP    %s", g_ui_state.ip);
        lv_label_set_text(g_home_ip, buf);
       // lv_label_set_text(g_home_hint, "KEY3 switch mode\nKEY1 start OTA");
        return;
    }

    // 4. 刷新 WIFI 页面
    if (g_pages[UI_PAGE_WIFI] && !lv_obj_has_flag(g_pages[UI_PAGE_WIFI], LV_OBJ_FLAG_HIDDEN)) {
        lv_label_set_text(g_wifi_mode_value, ui_mode_text());
        lv_label_set_text(g_wifi_ssid_value, g_ui_state.ssid);
        lv_label_set_text(g_wifi_connect_value, ui_connect_text());
        lv_label_set_text(g_wifi_ip_value, g_ui_state.ip);
        lv_label_set_text(g_wifi_web_ip_value, g_ui_state.apsta_mode ? g_ui_state.web_ip : "--");
        return;
    }

    // 5. 刷新 DOWNLOAD 页面
    if (g_pages[UI_PAGE_DOWNLOAD] && !lv_obj_has_flag(g_pages[UI_PAGE_DOWNLOAD], LV_OBJ_FLAG_HIDDEN)) {
        lv_label_set_text(g_download_file_value, g_ui_state.download_file);
        ui_format_size(g_ui_state.download_total_bytes, size_buf, sizeof(size_buf));
        lv_label_set_text(g_download_size_value, size_buf);
        snprintf(buf, sizeof(buf), "%.1f KB/s", g_ui_state.download_speed_kb);
        lv_label_set_text(g_download_speed_value, buf);
        snprintf(buf, sizeof(buf), "%02d:%02d", g_ui_state.download_eta_min, g_ui_state.download_eta_sec);
        lv_label_set_text(g_download_eta_value, buf);
        
        if (g_ui_state.download_done_bytes > 0 && g_ui_state.download_total_bytes > 0) {
            ui_format_size(g_ui_state.download_done_bytes, done_buf, sizeof(done_buf));
            ui_format_size(g_ui_state.download_total_bytes, total_buf, sizeof(total_buf));
            snprintf(buf, sizeof(buf), "%d%%  %s / %s", g_ui_state.download_percent, done_buf, total_buf);
        } else {
            snprintf(buf, sizeof(buf), "%d%%", g_ui_state.download_percent);
        }
        lv_label_set_text(g_download_percent_value, buf);
        lv_label_set_text(g_download_status_value, g_ui_state.ota_status);
        lv_bar_set_value(g_download_bar, g_ui_state.download_percent, LV_ANIM_ON);
    }
}

static void ui_status_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    ui_update_time();
}

static void ui_create_status_bar(void)
{
    lv_obj_t *bar = lv_obj_create(lv_scr_act());
    lv_obj_set_size(bar, 320, 42);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0xF6F8FC), 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_pad_left(bar, 14, 0);
    lv_obj_set_style_pad_right(bar, 14, 0);
    lv_obj_set_style_pad_top(bar, 8, 0);
    lv_obj_set_style_pad_bottom(bar, 8, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    g_time_label = lv_label_create(bar);
    lv_obj_set_style_text_font(g_time_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(g_time_label, lv_color_hex(0x162033), 0);
    lv_obj_align(g_time_label, LV_ALIGN_LEFT_MID, 0, 0);

    g_usb_badge = lv_label_create(bar);
    lv_label_set_text(g_usb_badge, "USB");
    lv_obj_set_style_radius(g_usb_badge, 12, 0);
    lv_obj_set_style_pad_left(g_usb_badge, 8, 0);
    lv_obj_set_style_pad_right(g_usb_badge, 8, 0);
    lv_obj_set_style_pad_top(g_usb_badge, 4, 0);
    lv_obj_set_style_pad_bottom(g_usb_badge, 4, 0);
    lv_obj_set_style_text_font(g_usb_badge, &lv_font_montserrat_12, 0);
    lv_obj_align(g_usb_badge, LV_ALIGN_RIGHT_MID, 0, 0);

    g_wifi_badge = lv_label_create(bar);
    lv_label_set_text(g_wifi_badge, LV_SYMBOL_WIFI " WiFi");
    lv_obj_set_style_radius(g_wifi_badge, 12, 0);
    lv_obj_set_style_pad_left(g_wifi_badge, 8, 0);
    lv_obj_set_style_pad_right(g_wifi_badge, 8, 0);
    lv_obj_set_style_pad_top(g_wifi_badge, 4, 0);
    lv_obj_set_style_pad_bottom(g_wifi_badge, 4, 0);
    lv_obj_set_style_text_font(g_wifi_badge, &lv_font_montserrat_12, 0);
    lv_obj_align_to(g_wifi_badge, g_usb_badge, LV_ALIGN_OUT_LEFT_MID, -8, 0);
}

static void ui_create_boot_page(void)
{
    lv_obj_t *page = ui_create_page();
    lv_obj_t *hero;
    lv_obj_t *title;
    lv_obj_t *spinner;

    g_pages[UI_PAGE_BOOT] = page;
    
    // 1. 卡片背景 (296x172)
    hero = ui_create_card(page, 296, 172);
    lv_obj_align(hero, LV_ALIGN_TOP_MID, 0, 0);
    
    // 【关键】强制关掉所有可能存在的自动布局和内边距
    lv_obj_set_layout(hero, 0); 
    lv_obj_set_style_pad_all(hero, 0, 0); 
    lv_obj_set_style_bg_color(hero, lv_color_hex(0xEAF1FF), 0);

    // 2. 标题文字：手动计算居中 (296/2 = 148)
    title = lv_label_create(hero);
    lv_label_set_text(title, "SYSTEM BOOTING");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x1A4FD8), 0);
    // 顶部 y=15，x=0居中对齐
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 15); 

    // 3. 进度条 (Spinner)：强制设置 X 轴在 296 宽度的物理中心
    spinner = lv_spinner_create(hero, 1000, 60);
    lv_obj_set_size(spinner, 70, 70);
    
    // 【暴力对齐】直接通过坐标设定：x=148(中心点)-35(一半宽度) = 113
    // y 坐标定在 65，避开上面的标题
    lv_obj_set_pos(spinner, 113, 65); 
    
    lv_obj_set_style_arc_width(spinner, 8, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(spinner, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_color(spinner, lv_color_hex(0x4F7CFF), LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(spinner, lv_color_hex(0xD8E4FF), LV_PART_MAIN);

    // 4. 进度文字 (g_boot_status)
    g_boot_status = lv_label_create(hero);
    lv_obj_set_width(g_boot_status, 280); // 宽度给足，方便居中
    lv_obj_set_style_text_align(g_boot_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(g_boot_status, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(g_boot_status, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(g_boot_status, lv_color_hex(0x24324A), 0);
    
    // 放在 Spinner 正下方，y = 65 + 70 + 10 = 145
    lv_obj_set_pos(g_boot_status, 8, 145); 
}

static void ui_create_home_page(void)
{
    lv_obj_t *page = ui_create_page();
    lv_obj_t *hero;
    lv_obj_t *title;
    lv_obj_t *card;

    g_pages[UI_PAGE_HOME] = page;

    hero = ui_create_card(page, 296, 78);
    lv_obj_align(hero, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(hero, lv_color_hex(0x1858F2), 0);

    g_home_status = lv_label_create(hero);
    lv_obj_set_width(g_home_status, 246);
    lv_label_set_long_mode(g_home_status, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(g_home_status, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(g_home_status, lv_color_hex(0xE7EEFF), 0);
    lv_obj_align(g_home_status, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    card = ui_create_card(page, 296, 88);
    lv_obj_align(card, LV_ALIGN_BOTTOM_MID, 0, 0);

    g_home_mode = lv_label_create(card);
    lv_obj_set_style_text_font(g_home_mode, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(g_home_mode, lv_color_hex(0x182033), 0);
    lv_obj_align(g_home_mode, LV_ALIGN_TOP_LEFT, 0, 0);

    g_home_ssid = lv_label_create(card);
    lv_obj_set_style_text_font(g_home_ssid, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(g_home_ssid, lv_color_hex(0x526079), 0);
    lv_obj_align(g_home_ssid, LV_ALIGN_TOP_LEFT, 0, 30);

    g_home_ip = lv_label_create(card);
    lv_obj_set_style_text_font(g_home_ip, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(g_home_ip, lv_color_hex(0x526079), 0);
    lv_obj_align(g_home_ip, LV_ALIGN_TOP_LEFT, 0, 52);

    g_home_hint = lv_label_create(card);
    lv_obj_set_width(g_home_hint, 132);
    lv_label_set_long_mode(g_home_hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(g_home_hint, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_font(g_home_hint, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(g_home_hint, lv_color_hex(0x7B8699), 0);
    lv_obj_align(g_home_hint, LV_ALIGN_RIGHT_MID, 0, 0);
}
static void ui_create_wifi_page(void)
{
    lv_obj_t *page = ui_create_page();
    g_pages[UI_PAGE_WIFI] = page;

    lv_obj_t *card = ui_create_card(page, 296, 172);
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 0);
    // 确保没有布局干扰
    lv_obj_set_layout(card, 0);
    lv_obj_set_style_pad_all(card, 12, 0);

    // 标题：WiFi Status
    lv_obj_t *title = lv_label_create(card);
    lv_label_set_text(title, "WiFi Connection");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x182033), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, -4);

    // --- 第一行：Mode & Connection ---
    // y轴从 32 开始
    g_wifi_mode_value    = ui_create_field(card, "Mode",       0,   32, 120);
    g_wifi_connect_value = ui_create_field(card, "Status",     145, 32, 120);
    
    // --- 第二行：SSID (独占一行或给足宽度) ---
    // y轴下移至 75
    g_wifi_ssid_value    = ui_create_field(card, "SSID",       0,   75, 260);
    
    // --- 第三行：IP 信息 (上下排布，防止 Web Server IP 飞出去) ---
    // y轴下移至 118
    g_wifi_ip_value      = ui_create_field(card, "Local IP",   0,   118, 130);
    // 缩短标题，宽度给够
    g_wifi_web_ip_value  = ui_create_field(card, "Web Server", 145, 118, 130);
}
static void ui_create_download_page(void)
{
    lv_obj_t *page = ui_create_page();
    g_pages[UI_PAGE_DOWNLOAD] = page;

    lv_obj_t *card = ui_create_card(page, 296, 172);
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 0);
    // 禁用自动布局，防止组件互相推挤
    lv_obj_set_layout(card, 0); 

    // 1. 标题
    lv_obj_t *title = lv_label_create(card);
    lv_label_set_text(title, "FILE DOWNLOAD");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x182033), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, -2); // 稍微往上提一点点

    // 2. 右侧状态文字
    g_download_status_value = lv_label_create(card);
    lv_obj_set_width(g_download_status_value, 120);
    lv_obj_set_style_text_align(g_download_status_value, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_font(g_download_status_value, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(g_download_status_value, lv_color_hex(0x4F7CFF), 0);
    lv_obj_align(g_download_status_value, LV_ALIGN_TOP_RIGHT, 0, 2);

    // --- 核心布局调整：压缩 Y 轴间距 ---
    
    // 第一行：文件名 (y=28)。宽度给足，防止换行导致的出框
    g_download_file_value = ui_create_field(card, "File Name", 0, 28, 270);
    // 强制不换行，长了就滚动
    lv_label_set_long_mode(g_download_file_value, LV_LABEL_LONG_SCROLL_CIRCULAR);

    // 第二行：Size & Speed (y=70)
    g_download_size_value  = ui_create_field(card, "Size",  0,   70, 130);
    g_download_speed_value = ui_create_field(card, "Speed", 145, 70, 130);

    // 第三行：ETA & Progress (y=112)
    g_download_eta_value     = ui_create_field(card, "ETA",      0,   112, 130);
    g_download_percent_value = ui_create_field(card, "Progress", 145, 112, 130);

    // 3. 底部进度条 (y轴固定对齐底部)
    g_download_bar = lv_bar_create(card);
    lv_obj_set_size(g_download_bar, 270, 8);
    // 关键：距离底部留 5 像素，防止被边框切掉
    lv_obj_align(g_download_bar, LV_ALIGN_BOTTOM_MID, 0, -5); 
    
    lv_bar_set_range(g_download_bar, 0, 100);
    lv_obj_set_style_bg_color(g_download_bar, lv_color_hex(0xDCE5F7), LV_PART_MAIN);
    lv_obj_set_style_bg_color(g_download_bar, lv_color_hex(0x4F7CFF), LV_PART_INDICATOR);
    lv_obj_set_style_radius(g_download_bar, 8, LV_PART_MAIN);
    lv_obj_set_style_radius(g_download_bar, 8, LV_PART_INDICATOR);
}

void ui_create(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0xEEF3FA), 0);
    lv_obj_set_style_bg_grad_color(scr, lv_color_hex(0xF9FBFE), 0);
    lv_obj_set_style_bg_grad_dir(scr, LV_GRAD_DIR_VER, 0);

    ui_create_status_bar();
    ui_create_boot_page();
    ui_create_home_page();
    ui_create_wifi_page();
    ui_create_download_page();
    ui_switch_page_inner(UI_PAGE_BOOT);
    ui_refresh();
    lv_timer_create(ui_status_timer_cb, 1000, NULL);
}

void lvgl_task(void *arg)
{
    lvgl_msg_t msg;
    (void)arg;

   // ui_create();

    while (1) {
        while (xQueueReceive(lvgl_queue, &msg, 0)) {
            switch (msg.type) {
            case LVGL_MSG_SET_STATUS:
                strlcpy(g_ui_state.status, msg.text, sizeof(g_ui_state.status));
                
                // 【新增判断】如果收到的字符串包含 "READY"，直接执行页面跳转，不给它重叠的机会
                if (strstr(msg.text, "READY") != NULL || strstr(msg.text, "Ready") != NULL) {
                    ui_switch_page_inner(UI_PAGE_WIFI); // 强制跳到 WIFI 界面
                }
                break;
                case LVGL_MSG_SET_SSID:
                    strlcpy(g_ui_state.ssid, msg.text, sizeof(g_ui_state.ssid));
                    break;
                case LVGL_MSG_SET_IP:
                    strlcpy(g_ui_state.ip, msg.text, sizeof(g_ui_state.ip));
                    break;
                case LVGL_MSG_SET_WEB_IP:
                    strlcpy(g_ui_state.web_ip, msg.text, sizeof(g_ui_state.web_ip));
                    break;
                case LVGL_MSG_SET_OTA_TEXT:
                    strlcpy(g_ui_state.ota_status, msg.text, sizeof(g_ui_state.ota_status));
                    strlcpy(g_ui_state.status, msg.text, sizeof(g_ui_state.status));
                    break;
                case LVGL_MSG_SET_WIFI_MODE:
                    g_ui_state.apsta_mode = (msg.value != 0);
                    break;
                case LVGL_MSG_SET_WIFI_CONNECTED:
                    g_ui_state.wifi_connected = (msg.value != 0);
                    break;
                case LVGL_MSG_SET_USB_CONNECTED:
                    g_ui_state.usb_connected = (msg.value != 0);
                    break;
                case LVGL_MSG_SHOW_PAGE:
                    ui_switch_page_inner((ui_page_t)msg.value);
                    break;
                case LVGL_MSG_SET_DOWNLOAD_META:
                    g_ui_state.download_total_bytes = (uint32_t)msg.value;
                    // 使用 strlcpy 替代 strncpy，确保安全截断且自动补 \0
                    strlcpy(g_ui_state.download_file, msg.text, sizeof(g_ui_state.download_file));
                    break;
                case LVGL_MSG_DOWNLOAD_PROGRESS:
                {
                    download_ui_msg_t *payload = (download_ui_msg_t *)msg.text;
                    g_ui_state.download_percent = payload->percentage;
                    g_ui_state.download_speed_kb = payload->speed_kb;
                    g_ui_state.download_eta_min = payload->eta_min;
                    g_ui_state.download_eta_sec = payload->eta_sec;
                    g_ui_state.download_done_bytes = payload->downloaded_bytes;
                    g_ui_state.download_total_bytes = payload->total_bytes;
                    break;
                }
                default:
                    break;
            }
            ui_refresh(); 
        }

        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void ui_show_page(ui_page_t page)
{
    lvgl_send_value(LVGL_MSG_SHOW_PAGE, (int32_t)page);
}

void ui_show_home(void)
{
    ui_show_page(UI_PAGE_HOME);
}

void ui_show_wifi(void)
{
    ui_show_page(UI_PAGE_WIFI);
}

void ui_show_download(void)
{
    ui_show_page(UI_PAGE_DOWNLOAD);
}

void ui_set_status(const char *text)
{
    lvgl_send_text(LVGL_MSG_SET_STATUS, text);
}

void ui_set_ssid(const char *text)
{
    lvgl_send_text(LVGL_MSG_SET_SSID, text);
}

void ui_set_ip(const char *text)
{
    lvgl_send_text(LVGL_MSG_SET_IP, text);
}

void ui_set_web_ip(const char *text)
{
    lvgl_send_text(LVGL_MSG_SET_WEB_IP, text);
}

void ui_set_ota(const char *text)
{
    lvgl_send_text(LVGL_MSG_SET_OTA_TEXT, text);
}

void ui_set_wifi_mode(bool is_apsta_mode)
{
    lvgl_send_value(LVGL_MSG_SET_WIFI_MODE, is_apsta_mode ? 1 : 0);
}

void ui_set_wifi_connected(bool connected)
{
    lvgl_send_value(LVGL_MSG_SET_WIFI_CONNECTED, connected ? 1 : 0);
}

void ui_set_usb_connected(bool connected)
{
    lvgl_send_value(LVGL_MSG_SET_USB_CONNECTED, connected ? 1 : 0);
}

void ui_set_download_info(const char *file_name, uint32_t total_bytes)
{
    lvgl_msg_t msg = {0};
    msg.type = LVGL_MSG_SET_DOWNLOAD_META;
    msg.value = (int32_t)total_bytes;
    if (file_name) {
        strncpy(msg.text, file_name, sizeof(msg.text) - 1);
    }
    xQueueSend(lvgl_queue, &msg, 0);
}

void ui_push_download(int percent,
                      float speed_kb,
                      int eta_min,
                      int eta_sec,
                      uint32_t downloaded_bytes,
                      uint32_t total_bytes)
{
    lvgl_msg_t msg = {0};
    download_ui_msg_t payload = {
        .percentage = percent,
        .speed_kb = speed_kb,
        .eta_min = eta_min,
        .eta_sec = eta_sec,
        .downloaded_bytes = downloaded_bytes,
        .total_bytes = total_bytes
    };

    msg.type = LVGL_MSG_DOWNLOAD_PROGRESS;
    memcpy(msg.text, &payload, sizeof(payload));
    xQueueSend(lvgl_queue, &msg, 0);
}
