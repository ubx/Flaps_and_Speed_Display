#ifndef NATIVE_TEST_BUILD

#include "esp_log.h"
#include "lvgl.h"

// Waveshare BSP for this exact board (handles power rails + CO5300 + pins)
#include "bsp/esp32_s3_touch_amoled_1_75.h"

static const char* TAG = "screen2";

static void ui_create_screen2()
{
    lv_obj_t* scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t* label = lv_label_create(scr);
    lv_label_set_text(label, "FLAPS");
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_48, 0);
    lv_obj_center(label);
}

void screen2_start()
{
    ESP_LOGI(TAG, "Starting Screen 2...");

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
    cfg.lv_adapter_cfg.task_core_id = 1;

    lv_display_t* disp = bsp_display_start_with_config(&cfg);
    if (!disp)
    {
        ESP_LOGE(TAG, "bsp_display_start_with_config() failed");
        return;
    }

    if (bsp_display_lock(-1) == ESP_OK)
    {
        ui_create_screen2();
        bsp_display_unlock();
    }

    ESP_LOGI(TAG, "Screen 2 ready");
}

#else
void screen2_start()
{
}
#endif
