#include <cmath>
#include "lvgl.h"
#include "../ui.h"
#include "../ui_helpers.hpp"

/* ================= CONFIG ================= */
#define WIND_MIN -180.0f
#define WIND_MAX 180.0f

#define NEEDLE_INNER_RADIUS 100
#define NEEDLE_OUTER_RADIUS 210
#define ARROW_LENGTH 20

/* ================= STATE ================= */
static lv_obj_t* s_screen = nullptr;
static lv_obj_t* s_scale = nullptr;
static lv_obj_t* s_needle = nullptr;
static lv_obj_t* s_label = nullptr;
static lv_obj_t* s_inner_circle = nullptr;
static StaleOverlayState s_stale_overlay;

extern const lv_font_t mono_digits_120;

/* ================= UTILS ================= */
static inline void make_noninteractive(lv_obj_t* o)
{
    if (!o) return;
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
}

/**
 * Custom needle update that supports an inner radius (gap from center)
 */
static void ui_set_line_needle_value(lv_obj_t* scale_obj, lv_obj_t* needle_line,
                                     const int32_t inner_length,
                                     const int32_t outer_length,
                                     int32_t value)
{
    lv_obj_align(needle_line, LV_ALIGN_TOP_LEFT, 0, 0);

    int32_t rotation = lv_scale_get_rotation(scale_obj);
    int32_t angle_range = lv_scale_get_angle_range(scale_obj);
    int32_t min = lv_scale_get_range_min_value(scale_obj);
    int32_t max = lv_scale_get_range_max_value(scale_obj);
    int32_t width = lv_obj_get_width(scale_obj);
    int32_t height = lv_obj_get_height(scale_obj);

    int32_t angle = 0;
    if (value > min)
    {
        if (value > max) angle = angle_range;
        else angle = (int32_t)((int64_t)angle_range * (value - min) / (max - min));
    }

    int32_t total_angle = rotation + angle;

    // ===== Geometry tuning =====
    const int32_t triangle_base_width = 24; // Width of the base at outer ring

    // Precompute sin/cos
    int32_t cos_a = lv_trigo_cos(total_angle);
    int32_t sin_a = lv_trigo_sin(total_angle);

    // Perpendicular vector (for base width)
    int32_t cos_p = lv_trigo_cos(total_angle + 90);
    int32_t sin_p = lv_trigo_sin(total_angle + 90);

    // ===== Points =====
    static lv_point_precise_t pts[4];

    // Tip (pointing to inner ring)
    pts[0].x = (width / 2) + ((inner_length * cos_a) >> LV_TRIGO_SHIFT);
    pts[0].y = (height / 2) + ((inner_length * sin_a) >> LV_TRIGO_SHIFT);

    // Base - left (at outer ring)
    pts[1].x = (width / 2)
        + ((outer_length * cos_a) >> LV_TRIGO_SHIFT)
        + (((triangle_base_width / 2) * cos_p) >> LV_TRIGO_SHIFT);
    pts[1].y = (height / 2)
        + ((outer_length * sin_a) >> LV_TRIGO_SHIFT)
        + (((triangle_base_width / 2) * sin_p) >> LV_TRIGO_SHIFT);

    // Base - right (at outer ring)
    pts[2].x = (width / 2)
        + ((outer_length * cos_a) >> LV_TRIGO_SHIFT)
        - (((triangle_base_width / 2) * cos_p) >> LV_TRIGO_SHIFT);
    pts[2].y = (height / 2)
        + ((outer_length * sin_a) >> LV_TRIGO_SHIFT)
        - (((triangle_base_width / 2) * sin_p) >> LV_TRIGO_SHIFT);

    // Close shape
    pts[3] = pts[0];

    lv_line_set_points(needle_line, pts, 4);
}

/* ================= GAUGE ================= */
static void ui_create_gauge()
{
    s_screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(s_screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);

    // Round scale -180..180 deg
    s_scale = lv_scale_create(s_screen);
    lv_obj_set_size(s_scale, 466, 466);
    lv_obj_center(s_scale);

    lv_scale_set_mode(s_scale, LV_SCALE_MODE_ROUND_INNER);
    lv_scale_set_range(s_scale, (int32_t)WIND_MIN, (int32_t)WIND_MAX);
    lv_scale_set_total_tick_count(s_scale, 37); // every 10 deg (360/10 + 1)
    lv_scale_set_major_tick_every(s_scale, 3);  // major each 30 deg
    lv_scale_set_angle_range(s_scale, 360);
    lv_scale_set_rotation(s_scale, 90); // 0 deg is UP
    lv_scale_set_label_show(s_scale, true);

    lv_obj_set_style_text_color(s_scale, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_scale, &lv_font_montserrat_20, 0);

    // Needle
    s_needle = lv_line_create(s_scale);
    lv_obj_set_style_line_width(s_needle, 8, 0);
    lv_obj_set_style_line_color(s_needle, lv_color_white(), 0);
    lv_obj_set_style_line_rounded(s_needle, true, 0);

    // Inner circle
    s_inner_circle = lv_obj_create(s_screen);
    lv_obj_set_size(s_inner_circle, NEEDLE_INNER_RADIUS * 2, NEEDLE_INNER_RADIUS * 2);
    lv_obj_center(s_inner_circle);
    lv_obj_set_style_radius(s_inner_circle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(s_inner_circle, LV_OPA_0, 0);
    lv_obj_set_style_border_color(s_inner_circle, lv_color_white(), 0);
    lv_obj_set_style_border_width(s_inner_circle, 4, 0);
    make_noninteractive(s_inner_circle);

    // Center value (Wind Speed)
    s_label = lv_label_create(s_screen);
    lv_obj_set_style_text_color(s_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_label, &mono_digits_120, 0);
    lv_label_set_text(s_label, "--");
    lv_obj_center(s_label);

    // Unit
    lv_obj_t* unit = lv_label_create(s_screen);
    lv_obj_set_style_text_color(unit, lv_color_white(), 0);
    lv_obj_set_style_text_font(unit, &lv_font_montserrat_16, 0);
    lv_label_set_text(unit, "km/h");
    lv_obj_align(unit, LV_ALIGN_CENTER, 0, 80);

    /* Title */
    lv_obj_t* title = lv_label_create(s_screen);
    make_noninteractive(title);
    lv_label_set_text(title, "Wind");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_bg_color(title, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(title, LV_OPA_COVER, 0);
    lv_obj_align(title, LV_ALIGN_BOTTOM_MID, 0, -10);

    // Initial position
    ui_set_line_needle_value(s_scale, s_needle, NEEDLE_INNER_RADIUS, NEEDLE_OUTER_RADIUS, 0);

    s_stale_overlay = {};
}

/* ================= TIMER ================= */
static void ui_update_timer_cb(lv_timer_t* /*t*/)
{
    if (lv_screen_active() != s_screen) return;

    const bool stale = is_stale();
    ui_set_stale_overlay(s_screen, s_stale_overlay, stale);

    float wind_speed = get_wind_speed_kmh();
    float wind_dir = get_wind_direction();
    float heading = get_heading();

    // Relative direction
    float rel_dir = wind_dir - heading + 180.0f;
    while (rel_dir > 180.0f) rel_dir -= 360.0f;
    while (rel_dir < -180.0f) rel_dir += 360.0f;

    // Smooth rel_dir (Exponential Moving Average)
    static float s_smoothed_rel_dir = 0.0f;
    static bool s_first_run = true;
    if (s_first_run)
    {
        s_smoothed_rel_dir = rel_dir;
        s_first_run = false;
    }
    else
    {
        float diff = rel_dir - s_smoothed_rel_dir;
        while (diff > 180.0f) diff -= 360.0f;
        while (diff < -180.0f) diff += 360.0f;
        s_smoothed_rel_dir += diff * 0.15f; // Alpha = 0.15 for smoothing

        // Normalize smoothed value
        while (s_smoothed_rel_dir > 180.0f) s_smoothed_rel_dir -= 360.0f;
        while (s_smoothed_rel_dir < -180.0f) s_smoothed_rel_dir += 360.0f;
    }

    if (s_scale && s_needle)
    {
        ui_set_line_needle_value(s_scale, s_needle, NEEDLE_INNER_RADIUS, NEEDLE_OUTER_RADIUS, (int32_t)lroundf(s_smoothed_rel_dir));
    }

    // Update label slower
    static uint8_t div = 0;
    div++;
    if (div >= 5)
    {
        div = 0;
        if (s_label)
        {
            lv_label_set_text_fmt(s_label, "%d", (int)lroundf(wind_speed));
        }
    }
}

/* ================= PUBLIC API ================= */
void screen6_create()
{
    ui_create_gauge();
    lv_timer_create(ui_update_timer_cb, 40, nullptr);
}

lv_obj_t* screen6_get()
{
    return s_screen;
}
