#include <cmath>

#ifndef NATIVE_BUILD

#include "esp_log.h"
#include "lvgl.h"

// Waveshare BSP for this exact board (handles power rails + CO5300 + pins)
#include "bsp/esp32_s3_touch_amoled_1_75.h"

// Provided by your main.cpp (C++ symbol)
extern float get_ias_kmh();

static const char *TAG = "display";

// UI objects
static lv_obj_t *s_scale  = nullptr;
static lv_obj_t *s_needle = nullptr;
static lv_obj_t *s_label  = nullptr;

static void ui_create_gauge()
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    // Round inner scale 40..280 km/h
    s_scale = lv_scale_create(scr);
    lv_obj_set_size(s_scale, 380, 380);
    lv_obj_center(s_scale);

    lv_scale_set_mode(s_scale, LV_SCALE_MODE_ROUND_INNER);
    lv_scale_set_range(s_scale, 40, 280);
    lv_scale_set_total_tick_count(s_scale, 25);   // minor ticks
    lv_scale_set_major_tick_every(s_scale, 5);    // major each 5th tick
    lv_scale_set_angle_range(s_scale, 280);
    lv_scale_set_rotation(s_scale, 130);
    lv_scale_set_label_show(s_scale, true);

    lv_obj_set_style_text_color(s_scale, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_scale, &lv_font_montserrat_14, 0);

    // Needle
    s_needle = lv_line_create(s_scale);
    lv_obj_set_style_line_width(s_needle, 8, 0);
    lv_obj_set_style_line_color(s_needle, lv_color_white(), 0);
    lv_obj_set_style_line_rounded(s_needle, true, 0);

    // Center value
    s_label = lv_label_create(scr);
    lv_obj_set_style_text_color(s_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_label, &lv_font_montserrat_14, 0);
    lv_label_set_text(s_label, "40");
    lv_obj_center(s_label);

    // Unit
    lv_obj_t *unit = lv_label_create(scr);
    lv_obj_set_style_text_color(unit, lv_color_white(), 0);
    lv_obj_set_style_text_font(unit, &lv_font_montserrat_14, 0);
    lv_label_set_text(unit, "km/h");
    lv_obj_align(unit, LV_ALIGN_CENTER, 0, 30);

    // Initial position
    lv_scale_set_line_needle_value(s_scale, s_needle, 20, 40);
}

static void ui_update_timer_cb(lv_timer_t * /*t*/)
{
    float v = get_ias_kmh();
    if (std::isnan(v) || std::isinf(v)) v = 0.0f;
    if (v < 40.0f) v = 40.0f;
    if (v > 280.0f) v = 280.0f;

    const int32_t vi = (int32_t)(v + 0.5f);

    if (s_scale && s_needle) {
        lv_scale_set_line_needle_value(s_scale, s_needle, 100, vi);
    }
    if (s_label) {
        lv_label_set_text_fmt(s_label, "%d", (int)vi);
    }
}

void display_start()
{
    ESP_LOGI(TAG, "Starting Waveshare BSP display...");

    // This initializes:
    // - correct SPI host (SPI2 on this board)
    // - correct pins (PCLK=38, CS=12, D0..D3=4..7, RST=39)
    // - power / panel init sequence
    // Configure LVGL adapter to run its task on CPU1 to prevent starving IDLE0 watchdog on CPU0
    bsp_display_cfg_t cfg = {
        .lv_adapter_cfg = ESP_LV_ADAPTER_DEFAULT_CONFIG(),
        .rotation = ESP_LV_ADAPTER_ROTATE_0,
        .tear_avoid_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_NONE,
        .touch_flags = {
            .swap_xy = 0,
            .mirror_x = 1,
            .mirror_y = 1
        }
    };
    cfg.lv_adapter_cfg.task_core_id = 1; // Pin LVGL task to CPU1

    lv_display_t *disp = bsp_display_start_with_config(&cfg);
    if (!disp) {
        ESP_LOGE(TAG, "bsp_display_start_with_config() failed");
        return;
    }

    // Build UI
    ui_create_gauge();

    // Update at 10 Hz inside LVGL context (no extra FreeRTOS LVGL tasks needed)
    lv_timer_create(ui_update_timer_cb, 100, nullptr);

    ESP_LOGI(TAG, "Display UI ready");
}

#else
void display_start() {}
#endif
