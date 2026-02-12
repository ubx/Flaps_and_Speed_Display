#include <cmath>
#ifndef NATIVE_BUILD
#include "lvgl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Forward declared in main.cpp
float get_ias_kmh();

static lv_obj_t* s_scale = nullptr;
static lv_obj_t* s_needle = nullptr;
static lv_obj_t* s_value_label = nullptr;
static lv_display_t* s_disp = nullptr;

// Minimal flush callback to satisfy LVGL when no HW driver is wired yet
static void dummy_flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map)
{
    LV_UNUSED(disp);
    LV_UNUSED(area);
    LV_UNUSED(px_map);
    // Immediately signal ready; no actual HW transfer here
    lv_display_flush_ready(disp);
}

static void create_speed_gauge()
{
    // Create a round inner scale acting as a speedometer: 40..280 km/h
    s_scale = lv_scale_create(lv_screen_active());
    lv_obj_center(s_scale);
    lv_obj_set_size(s_scale, 220, 220);

    lv_scale_set_mode(s_scale, LV_SCALE_MODE_ROUND_INNER);
    lv_scale_set_range(s_scale, 40, 280);
    lv_scale_set_total_tick_count(s_scale, 25);      // minor ticks
    lv_scale_set_major_tick_every(s_scale, 5);       // major every 5th
    lv_scale_set_angle_range(s_scale, 280);
    lv_scale_set_rotation(s_scale, 130);
    lv_scale_set_label_show(s_scale, true);

    // Styling
    lv_obj_set_style_length(s_scale, 8, LV_PART_ITEMS);
    lv_obj_set_style_length(s_scale, 14, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(s_scale, 16, LV_PART_MAIN);

    // Needle
    s_needle = lv_line_create(s_scale);
    lv_obj_set_style_line_color(s_needle, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_line_width(s_needle, 10, LV_PART_MAIN);
    lv_obj_set_style_length(s_needle, 22, LV_PART_MAIN);
    lv_obj_set_style_line_rounded(s_needle, true, LV_PART_MAIN);
    lv_obj_set_style_pad_right(s_needle, 46, LV_PART_MAIN);

    // Center circle with value + unit
    lv_obj_t* center = lv_obj_create(lv_scr_act());
    lv_obj_set_size(center, 130, 130);
    lv_obj_center(center);
    lv_obj_set_style_radius(center, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(center, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(center, 0, 0);
    lv_obj_set_style_pad_all(center, 8, 0);

    s_value_label = lv_label_create(center);
    lv_obj_center(s_value_label);
    lv_label_set_text(s_value_label, "0");

    lv_obj_t* unit = lv_label_create(center);
    lv_label_set_text(unit, "km/h");
    lv_obj_align(unit, LV_ALIGN_BOTTOM_MID, 0, -8);

    // Set initial needle value
    lv_scale_set_line_needle_value(s_scale, s_needle, 0, 40);
}

static void lvgl_task(void* /*arg*/)
{
    const uint32_t period_ms = 10; // LVGL tick + handler period
    uint32_t accum = 0;

    while (true)
    {
        // Update LVGL tick and process timers/animations
        lv_tick_inc(period_ms);
        lv_timer_handler();

        // Update gauge at ~10 Hz
        accum += period_ms;
        if (accum >= 100) {
            accum = 0;
            float v = get_ias_kmh();            // IAS already in km/h
            if (std::isnan(v) || std::isinf(v)) v = 0.0f;
            if (v < 40.0f) v = 40.0f;
            if (v > 280.0f) v = 280.0f;

            if (s_scale && s_needle) {
                lv_scale_set_line_needle_value(s_scale, s_needle, 0, static_cast<int32_t>(v));
            }
            if (s_value_label) {
                lv_label_set_text_fmt(s_value_label, "%d", static_cast<int>(v + 0.5f));
            }
        }

        vTaskDelay(pdMS_TO_TICKS(period_ms));
    }
}

void display_start()
{
    // Initialize LVGL once
    static bool inited = false;
    if (!inited) {
        inited = true;
        lv_init();

        // If no default display is registered, create a minimal one to allow object creation.
        s_disp = lv_display_get_default();
        if (!s_disp) {
            const uint16_t hor_res = 240;
            const uint16_t ver_res = 240;
            s_disp = lv_display_create(hor_res, ver_res);

            // Single small draw buffer (partial rendering)
            static lv_color_t draw_buf1[hor_res * 40]; // 1-line chunk * 40
            lv_display_set_buffers(s_disp, draw_buf1, nullptr, sizeof(draw_buf1), LV_DISPLAY_RENDER_MODE_PARTIAL);

            // Dummy flush until a real HW driver is wired
            lv_display_set_flush_cb(s_disp, dummy_flush_cb);
        }

        create_speed_gauge();

        // Start LVGL/ticker task
        xTaskCreatePinnedToCore(lvgl_task, "lvgl", 4096, nullptr, 5, nullptr, tskNO_AFFINITY);
    }
}

#else  // NATIVE_BUILD

void display_start() {}

#endif
