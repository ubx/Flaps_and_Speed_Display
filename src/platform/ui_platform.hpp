#pragma once

#include "lvgl.h"

bool ui_platform_init_display();
bool ui_platform_lock(int timeout_ms = -1);
void ui_platform_unlock();
int ui_platform_get_brightness();
void ui_platform_set_brightness(int brightness_percent);
lv_display_t* ui_platform_get_display();
