#include "lvgl.h"
#include "../ui.h"
#include "../ui_helpers.hpp"

static lv_obj_t* s_screen = nullptr;

static void ui_create_polar()
{
    s_screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(s_screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);

    /* Title */
    lv_obj_t* title = lv_label_create(s_screen);
    lv_label_set_text(title, "Polar");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_align(title, LV_ALIGN_BOTTOM_MID, 0, -10);

    /* Main Label (placeholder) */
    lv_obj_t* label = lv_label_create(s_screen);
    lv_label_set_text(label, "Polar data\n(Coming soon)");
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label);
}

void screen7_create()
{
    ui_create_polar();
}

lv_obj_t* screen7_get()
{
    return s_screen;
}
