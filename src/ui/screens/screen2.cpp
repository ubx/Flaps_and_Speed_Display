#ifndef NATIVE_TEST_BUILD

#include "esp_log.h"
#include "lvgl.h"
#include "../../flaputils.hpp"

// Waveshare BSP for this exact board (handles power rails + CO5300 + pins)
#include "bsp/esp32_s3_touch_amoled_1_75.h"

extern const lv_font_t digits_96;

extern flaputils::FlapSymbolResult get_flap_actual();

static lv_obj_t* s_screen = nullptr;
static lv_obj_t* s_flap_label = nullptr;

static void ui_update_timer_cb(lv_timer_t* /*t*/)
{
    if (s_flap_label)
    {
        flaputils::FlapSymbolResult res = get_flap_actual();
        lv_label_set_text(s_flap_label, res.symbol ? res.symbol : "-");
    }
}

static void ui_create_screen2()
{
    s_screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(s_screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);

    // Title
    lv_obj_t* title = lv_label_create(s_screen);
    lv_label_set_text(title, "Faps Act");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 80);

    // Dynamic Flap Value
    s_flap_label = lv_label_create(s_screen);
    lv_label_set_text(s_flap_label, "-");
    lv_obj_set_style_text_color(s_flap_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_flap_label, &digits_96, 0);
    lv_obj_center(s_flap_label);
}

void screen2_create()
{
    ui_create_screen2();
    lv_timer_create(ui_update_timer_cb, 100, nullptr);
}

lv_obj_t* screen2_get()
{
    return s_screen;
}

#else
void screen2_create()
{
}

lv_obj_t* screen2_get()
{
    return nullptr;
}
#endif
