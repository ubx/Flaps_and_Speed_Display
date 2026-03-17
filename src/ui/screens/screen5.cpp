#include "lvgl.h"
#include "../ui.h"
#include <cstdio>

static lv_obj_t* s_screen = nullptr;
static lv_obj_t* s_label_alt = nullptr;
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
    snprintf(buf, sizeof(buf), "%.0f m", get_alt_m());
    lv_label_set_text(s_label_alt, buf);
}

void screen5_create()
{
    s_screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(s_screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);

    lv_obj_t* title = lv_label_create(s_screen);
    lv_label_set_text(title, "Altitude");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_align(title, LV_ALIGN_BOTTOM_MID, 0, -10);

    s_label_alt = lv_label_create(s_screen);
    lv_obj_set_style_text_color(s_label_alt, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_label_alt, &lv_font_montserrat_20, 0);
    lv_obj_align(s_label_alt, LV_ALIGN_CENTER, 0, 0);

    s_stale_cross_a = nullptr;
    s_stale_cross_b = nullptr;
    s_stale_overlay_visible = false;

    lv_timer_create(ui_update_timer_cb, 500, nullptr);
}

lv_obj_t* screen5_get()
{
    return s_screen;
}
