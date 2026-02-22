#ifndef NATIVE_TEST_BUILD

#include "esp_log.h"
#include "lvgl.h"

// Waveshare BSP for this exact board (handles power rails + CO5300 + pins)
#include "bsp/esp32_s3_touch_amoled_1_75.h"

static const char* TAG = "screen3";
static lv_obj_t* s_screen = nullptr;

static void brightness_slider_event_cb(lv_event_t* e)
{
    lv_obj_t* slider = (lv_obj_t*)lv_event_get_target(e);
    int brightness = (int)lv_slider_get_value(slider);
    bsp_display_brightness_set(brightness);
    ESP_LOGI(TAG, "Brightness set to %d%%", brightness);
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
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 10);

    // Brightness Slider
    lv_obj_t* slider = lv_slider_create(s_screen);
    lv_obj_set_width(slider, 200);
    lv_slider_set_range(slider, 0, 100);
    lv_slider_set_value(slider, bsp_display_brightness_get(), LV_ANIM_OFF);
    lv_obj_align(slider, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(slider, brightness_slider_event_cb, LV_EVENT_VALUE_CHANGED, nullptr);

    lv_obj_t* slider_label = lv_label_create(s_screen);
    lv_label_set_text(slider_label, "Brightness");
    lv_obj_set_style_text_color(slider_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(slider_label, &lv_font_montserrat_16, 0);
    lv_obj_align_to(slider_label, slider, LV_ALIGN_OUT_TOP_MID, 0, -10);
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
