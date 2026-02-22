#ifndef NATIVE_TEST_BUILD

#include "esp_log.h"
#include "lvgl.h"

// Waveshare BSP for this exact board (handles power rails + CO5300 + pins)
#include "bsp/esp32_s3_touch_amoled_1_75.h"

static const char* TAG = "screen3";
static lv_obj_t* s_screen = nullptr;

static void ui_create_screen3()
{
    s_screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(s_screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);

    lv_obj_t* label = lv_label_create(s_screen);
    lv_label_set_text(label, "Settings");
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_48, 0);
    lv_obj_center(label);
}

void screen3_create()
{
    ui_create_screen3();
}

lv_obj_t* screen3_get()
{
    return s_screen;
}

#else
void screen3_create()
{
}

lv_obj_t* screen3_get()
{
    return nullptr;
}
#endif
