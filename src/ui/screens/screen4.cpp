#ifndef NATIVE_TEST_BUILD

#include "esp_log.h"
#include "lvgl.h"
#include "../../flaputils.hpp"
#include "bsp/esp32_s3_touch_amoled_1_75.h"
#include <cstdio>

extern float get_ias_kmh();
extern float get_weight_kg();
extern flaputils::FlapSymbolResult get_flap_actual();
extern flaputils::FlapSymbolResult get_flap_target();
extern bool is_stale();

static lv_obj_t* s_screen = nullptr;
static lv_obj_t* s_label_ias = nullptr;
static lv_obj_t* s_label_weight = nullptr;
static lv_obj_t* s_label_flap_actual = nullptr;
static lv_obj_t* s_label_flap_target = nullptr;
static lv_obj_t* s_stale_cross_a = nullptr;
static lv_obj_t* s_stale_cross_b = nullptr;
static bool s_stale_overlay_visible = false;

static void ui_set_stale_overlay(bool show)
{
    if (show == s_stale_overlay_visible) return;
    s_stale_overlay_visible = show;

    if (show)
    {
        if (!s_stale_cross_a)
        {
            static lv_point_precise_t cross_a_pts[2] = {{60, 60}, {406, 406}};
            s_stale_cross_a = lv_line_create(s_screen);
            lv_line_set_points(s_stale_cross_a, cross_a_pts, 2);
            lv_obj_set_style_line_width(s_stale_cross_a, 10, 0);
            lv_obj_set_style_line_color(s_stale_cross_a, lv_palette_main(LV_PALETTE_RED), 0);
            lv_obj_set_style_line_rounded(s_stale_cross_a, true, 0);
        }
        lv_obj_remove_flag(s_stale_cross_a, LV_OBJ_FLAG_HIDDEN);
        if (!s_stale_cross_b)
        {
            static lv_point_precise_t cross_b_pts[2] = {{406, 60}, {60, 406}};
            s_stale_cross_b = lv_line_create(s_screen);
            lv_line_set_points(s_stale_cross_b, cross_b_pts, 2);
            lv_obj_set_style_line_width(s_stale_cross_b, 10, 0);
            lv_obj_set_style_line_color(s_stale_cross_b, lv_palette_main(LV_PALETTE_RED), 0);
            lv_obj_set_style_line_rounded(s_stale_cross_b, true, 0);
        }
        lv_obj_remove_flag(s_stale_cross_b, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    if (s_stale_cross_a) lv_obj_add_flag(s_stale_cross_a, LV_OBJ_FLAG_HIDDEN);
    if (s_stale_cross_b) lv_obj_add_flag(s_stale_cross_b, LV_OBJ_FLAG_HIDDEN);
}

static void ui_update_timer_cb(lv_timer_t* timer)
{
    if (!s_screen || lv_screen_active() != s_screen) return;
    ui_set_stale_overlay(is_stale());

    char buf[64];

    // IAS
    snprintf(buf, sizeof(buf), "IAS: %.0f km/h", get_ias_kmh());
    lv_label_set_text(s_label_ias, buf);

    // Weight
    snprintf(buf, sizeof(buf), "Weight: %.0f kg", get_weight_kg());
    lv_label_set_text(s_label_weight, buf);

    // Flap Actual
    flaputils::FlapSymbolResult actual = get_flap_actual();
    const char* actual_name = flaputils::get_flap_symbol_name(actual.index);
    snprintf(buf, sizeof(buf), "Flap Actual: %s (%d)", actual_name ? actual_name : "---", actual.index);
    lv_label_set_text(s_label_flap_actual, buf);

    // Flap Target
    flaputils::FlapSymbolResult target = get_flap_target();
    const char* target_name = flaputils::get_flap_symbol_name(target.index);
    snprintf(buf, sizeof(buf), "Flap Target: %s (%d)", target_name ? target_name : "---", target.index);
    lv_label_set_text(s_label_flap_target, buf);
}

void screen4_create()
{
    s_screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(s_screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);

    lv_obj_t* title = lv_label_create(s_screen);
    lv_label_set_text(title, "Live Params");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    s_label_ias = lv_label_create(s_screen);
    lv_obj_set_style_text_color(s_label_ias, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_label_ias, &lv_font_montserrat_20, 0);
    lv_obj_align(s_label_ias, LV_ALIGN_TOP_MID, 0, 70);

    s_label_weight = lv_label_create(s_screen);
    lv_obj_set_style_text_color(s_label_weight, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_label_weight, &lv_font_montserrat_20, 0);
    lv_obj_align(s_label_weight, LV_ALIGN_TOP_MID, 0, 110);

    s_label_flap_actual = lv_label_create(s_screen);
    lv_obj_set_style_text_color(s_label_flap_actual, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_label_flap_actual, &lv_font_montserrat_20, 0);
    lv_obj_align(s_label_flap_actual, LV_ALIGN_TOP_MID, 0, 150);

    s_label_flap_target = lv_label_create(s_screen);
    lv_obj_set_style_text_color(s_label_flap_target, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_label_flap_target, &lv_font_montserrat_20, 0);
    lv_obj_align(s_label_flap_target, LV_ALIGN_TOP_MID, 0, 190);

    s_stale_cross_a = nullptr;
    s_stale_cross_b = nullptr;
    s_stale_overlay_visible = false;

    lv_timer_create(ui_update_timer_cb, 500, nullptr);
}

lv_obj_t* screen4_get()
{
    return s_screen;
}

#endif
