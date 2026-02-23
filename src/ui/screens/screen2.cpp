#ifndef NATIVE_TEST_BUILD

#include "esp_log.h"
#include "lvgl.h"
#include "../../flaputils.hpp"

// Waveshare BSP for this exact board (handles power rails + CO5300 + pins)
#include "bsp/esp32_s3_touch_amoled_1_75.h"

extern const lv_font_t digits_120;

extern flaputils::FlapSymbolResult get_flap_actual();

static lv_obj_t* s_screen = nullptr;
static lv_obj_t* s_flap_label = nullptr;
static lv_obj_t* s_scale = nullptr;
static const char* s_flap_symbols[32];

static void ui_update_timer_cb(lv_timer_t* /*t*/)
{
    if (lv_screen_active() != s_screen) return;

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
    lv_label_set_text(title, "Faps");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 80);

    // Dynamic Flap Value
    s_flap_label = lv_label_create(s_screen);
    lv_label_set_text(s_flap_label, "-");
    lv_obj_set_style_text_color(s_flap_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_flap_label, &digits_120, 0);
    lv_obj_center(s_flap_label);

    // Gauge (Scale)
    s_scale = lv_scale_create(s_screen);
    lv_obj_set_size(s_scale, 466, 466);
    lv_obj_center(s_scale);
    lv_scale_set_mode(s_scale, LV_SCALE_MODE_ROUND_INNER);
    lv_scale_set_angle_range(s_scale, 270);
    lv_scale_set_rotation(s_scale, 135);

    auto params = flaputils::get_flap_params();
    if (!params.empty())
    {
        uint32_t count = (uint32_t)params.size();
        lv_scale_set_range(s_scale, 0, count - 1);
        lv_scale_set_total_tick_count(s_scale, count);
        lv_scale_set_major_tick_every(s_scale, 1);
        lv_scale_set_label_show(s_scale, true);

        for (uint32_t i = 0; i < count && i < 31; ++i)
        {
            s_flap_symbols[i] = params[i].symbol;
        }
        s_flap_symbols[std::min(count, (uint32_t)31)] = nullptr;
        lv_scale_set_text_src(s_scale, s_flap_symbols);
    }

    lv_obj_set_style_text_color(s_scale, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_scale, &lv_font_montserrat_20, 0);
    lv_obj_set_style_line_color(s_scale, lv_color_white(), LV_PART_INDICATOR);
    lv_obj_set_style_line_width(s_scale, 4, LV_PART_INDICATOR);
}

void screen2_create()
{
    ui_create_screen2();
    lv_timer_create(ui_update_timer_cb, 200, nullptr);
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
