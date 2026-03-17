#include "lvgl.h"
#include "../../platform/ui_platform.hpp"
extern const lv_font_t digits_120;

static lv_obj_t* s_screen = nullptr;

static void brightness_plus_event_cb(lv_event_t* e)
{
    int brightness = ui_platform_get_brightness();
    brightness += 10;
    if (brightness > 100) brightness = 100;
    ui_platform_set_brightness(brightness);
}

static void brightness_minus_event_cb(lv_event_t* e)
{
    int brightness = ui_platform_get_brightness();
    brightness -= 10;
    if (brightness < 30) brightness = 30;
    ui_platform_set_brightness(brightness);
}

static void ui_create_screen3()
{
    s_screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(s_screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);

    lv_obj_t* label = lv_label_create(s_screen);
    lv_label_set_text(label, "Settings");
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
    lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, -10);

    // Brightness label
    lv_obj_t* brightness_label = lv_label_create(s_screen);
    lv_label_set_text(brightness_label, "Brightness");
    lv_obj_set_style_text_color(brightness_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(brightness_label, &lv_font_montserrat_20, 0);
    lv_obj_align(brightness_label, LV_ALIGN_CENTER, 0, -110);

    // Plus Button
    lv_obj_t* btn_plus = lv_btn_create(s_screen);
    lv_obj_set_size(btn_plus, 120, 120);
    lv_obj_align(btn_plus, LV_ALIGN_CENTER, 0, -40);
    lv_obj_add_event_cb(btn_plus, brightness_plus_event_cb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* label_plus = lv_label_create(btn_plus);
    lv_label_set_text(label_plus, "+");
    lv_obj_set_style_text_font(label_plus, &digits_120, 0);
    lv_obj_center(label_plus);

    // Minus Button
    lv_obj_t* btn_minus = lv_btn_create(s_screen);
    lv_obj_set_size(btn_minus, 120, 120);
    lv_obj_align(btn_minus, LV_ALIGN_CENTER, 0, 90);
    lv_obj_add_event_cb(btn_minus, brightness_minus_event_cb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* label_minus = lv_label_create(btn_minus);
    lv_label_set_text(label_minus, "-");
    lv_obj_set_style_text_font(label_minus, &digits_120, 0);
    lv_obj_center(label_minus);
}

void screen3_create()
{
    ui_create_screen3();
}

lv_obj_t* screen3_get()
{
    return s_screen;
}
