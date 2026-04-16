#ifndef UI_LVGL_H
#define UI_LVGL_H
#include "lvgl.h"

void ui_init(void);

void ui_home_update(const char *ssid, const char *ip, int mode);
void ui_init_update(const char *msg, lv_color_t color);
void ui_ota_update(const char *msg);


#endif /* OBS_HTTP_SINGLE_H */