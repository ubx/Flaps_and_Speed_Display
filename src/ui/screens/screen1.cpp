#include <cmath>

#ifndef NATIVE_TEST_BUILD

#include "esp_log.h"
#include "lvgl.h"
#include "../../flaputils.hpp"

// Waveshare BSP for this exact board (handles power rails + CO5300 + pins)
#include "bsp/esp32_s3_touch_amoled_1_75.h"

// Provided by your main.cpp (C++ symbols)
extern float get_ias_kmh();

// UI objects
extern const lv_font_t digits_120;
static lv_obj_t* s_screen = nullptr;
static lv_obj_t* s_scale = nullptr;
static lv_obj_t* s_needle = nullptr;
static lv_obj_t* s_label = nullptr;

// Needle dimensions
static constexpr int32_t NEEDLE_INNER_RADIUS = 130;
static constexpr int32_t NEEDLE_OUTER_RADIUS = 170;

/**
 * Custom needle update that supports an inner radius (gap from center)
 */
static void ui_set_line_needle_value(lv_obj_t* scale_obj, lv_obj_t* needle_line, const int32_t inner_length,
                                     const int32_t outer_length, int32_t value)
{
    lv_obj_align(needle_line, LV_ALIGN_TOP_LEFT, 0, 0);

    int32_t rotation = lv_scale_get_rotation(scale_obj);
    int32_t angle_range = lv_scale_get_angle_range(scale_obj);
    int32_t min = lv_scale_get_range_min_value(scale_obj);
    int32_t max = lv_scale_get_range_max_value(scale_obj);
    int32_t width = lv_obj_get_style_width(scale_obj, LV_PART_MAIN);
    int32_t height = lv_obj_get_style_height(scale_obj, LV_PART_MAIN);

    int32_t angle = 0;
    if (value > min)
    {
        if (value > max) angle = angle_range;
        else angle = static_cast<int32_t>((int64_t)angle_range * (value - min) / (max - min));
    }

    int32_t total_angle = rotation + angle;

    static lv_point_precise_t points[2];
    points[0].x = (width / 2) + ((inner_length * lv_trigo_cos(total_angle)) >> LV_TRIGO_SHIFT);
    points[0].y = (height / 2) + ((inner_length * lv_trigo_sin(total_angle)) >> LV_TRIGO_SHIFT);
    points[1].x = (width / 2) + ((outer_length * lv_trigo_cos(total_angle)) >> LV_TRIGO_SHIFT);
    points[1].y = (height / 2) + ((outer_length * lv_trigo_sin(total_angle)) >> LV_TRIGO_SHIFT);

    lv_line_set_points(needle_line, points, 2);
}

static void ui_create_gauge()
{
    s_screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(s_screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);

    // Round inner scale 40..280 km/h
    s_scale = lv_scale_create(s_screen);
    lv_obj_set_size(s_scale, 466, 466);
    lv_obj_center(s_scale);

    lv_scale_set_mode(s_scale, LV_SCALE_MODE_ROUND_INNER);
    lv_scale_set_range(s_scale, 40, 280);
    lv_scale_set_total_tick_count(s_scale, 25); // minor ticks
    lv_scale_set_major_tick_every(s_scale, 2); // major each 5th tick
    lv_scale_set_angle_range(s_scale, 280);
    lv_scale_set_rotation(s_scale, 130);
    lv_scale_set_label_show(s_scale, true);

    lv_obj_set_style_text_color(s_scale, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_scale, &lv_font_montserrat_20, 0);

    // Needle
    s_needle = lv_line_create(s_scale);
    lv_obj_set_style_line_width(s_needle, 12, 0);
    lv_obj_set_style_line_color(s_needle, lv_color_white(), 0);
    lv_obj_set_style_line_rounded(s_needle, true, 0);

    // Center value
    s_label = lv_label_create(s_screen);
    lv_obj_set_style_text_color(s_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_label, &digits_120, 0);
    lv_label_set_text(s_label, "--");
    lv_obj_center(s_label);

    // Unit
    lv_obj_t* unit = lv_label_create(s_screen);
    lv_obj_set_style_text_color(unit, lv_color_white(), 0);
    lv_obj_set_style_text_font(unit, &lv_font_montserrat_16, 0);
    lv_label_set_text(unit, "km/h");
    lv_obj_align(unit, LV_ALIGN_CENTER, 0, 80);

    // Initial position
    ui_set_line_needle_value(s_scale, s_needle, NEEDLE_INNER_RADIUS, NEEDLE_OUTER_RADIUS, 0);
}

static void ui_update_timer_cb(lv_timer_t* /*t*/)
{
    float v = get_ias_kmh();
    if (std::isnan(v) || std::isinf(v)) v = 0.0f;
    if (v < 40.0f) v = 40.0f;
    if (v > 280.0f) v = 280.0f;

    const int32_t vi = static_cast<int32_t>(v + 0.5f);

    if (s_scale && s_needle)
    {
        ui_set_line_needle_value(s_scale, s_needle, NEEDLE_INNER_RADIUS, NEEDLE_OUTER_RADIUS, vi);
    }
    if (s_label)
    {
        lv_label_set_text_fmt(s_label, "%d", static_cast<int>(vi));
    }
}

void screen1_create()
{
    ui_create_gauge();

    // Update at 10 Hz inside LVGL context (no extra FreeRTOS LVGL tasks needed)
    lv_timer_create(ui_update_timer_cb, 100, nullptr);
}

lv_obj_t* screen1_get()
{
    return s_screen;
}

#else
void screen1_create()
{
}

lv_obj_t* screen1_get()
{
    return nullptr;
}
#endif
