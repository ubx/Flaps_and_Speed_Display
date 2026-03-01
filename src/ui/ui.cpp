#include "ui.h"
#include "lvgl.h"
#include "screens/screen1.hpp"
#include "screens/screen2.hpp"
#include "screens/screen3.hpp"
#include "screens/screen4.hpp"
#include "esp_log.h"
#include "bsp/esp32_s3_touch_amoled_1_75.h"

static const char* TAG = "ui";
static lv_obj_t* s_label1 = nullptr;
static lv_obj_t* s_label2 = nullptr;
static lv_obj_t* s_label3 = nullptr;

void set_label1(const char* text)
{
    if (bsp_display_lock(-1) == ESP_OK)
    {
        if (s_label1) lv_label_set_text(s_label1, text);
        bsp_display_unlock();
    }
}

void set_label2(const char* text)
{
    if (bsp_display_lock(-1) == ESP_OK)
    {
        if (s_label2) lv_label_set_text(s_label2, text);
        bsp_display_unlock();
    }
}

void set_label3(const char* text)
{
    if (bsp_display_lock(-1) == ESP_OK)
    {
        if (s_label3) lv_label_set_text(s_label3, text);
        bsp_display_unlock();
    }
}

void ui_init(void)
{
    ESP_LOGI(TAG, "Initializing UI...");

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
        // Set background color for the splash screen
        lv_obj_t* act_scr = lv_screen_active();
        lv_obj_set_style_bg_color(act_scr, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(act_scr, LV_OPA_COVER, 0);

        // Simple splash screen labels
        s_label1 = lv_label_create(act_scr);
        lv_obj_set_style_text_color(s_label1, lv_color_white(), 0);
        lv_obj_set_style_text_font(s_label1, &lv_font_montserrat_20, 0);
        lv_obj_align(s_label1, LV_ALIGN_CENTER, 0, -50);
        lv_label_set_text(s_label1, "Loading...");

        s_label2 = lv_label_create(act_scr);
        lv_obj_set_style_text_color(s_label2, lv_color_white(), 0);
        lv_obj_set_style_text_font(s_label2, &lv_font_montserrat_16, 0);
        lv_obj_align(s_label2, LV_ALIGN_CENTER, 0, 0);
        lv_label_set_text(s_label2, "Please wait");

        s_label3 = lv_label_create(act_scr);
        lv_obj_set_style_text_color(s_label3, lv_color_white(), 0);
        lv_obj_set_style_text_font(s_label3, &lv_font_montserrat_16, 0);
        lv_obj_align(s_label3, LV_ALIGN_CENTER, 0, 40);
        lv_label_set_text(s_label3, "");

        screen1_create();
        screen2_create();
        screen3_create();
        screen4_create();

        /* Gestures:
           - UP/DOWN: cycle between screen1 <-> screen2
           - RIGHT SWIPE: enter/toggle screen3 and screen4
           - LEFT SWIPE: return to screen1 from screen3/screen4
        */
        auto gesture_cb = [](lv_event_t* e) {
            if (lv_event_get_code(e) != LV_EVENT_GESTURE) return;

            lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());

            if (dir == LV_DIR_BOTTOM) {
                if (lv_screen_active() == screen1_get()) {
                    lv_screen_load_anim(screen2_get(), LV_SCR_LOAD_ANIM_MOVE_BOTTOM, 300, 0, false);
                } else if (lv_screen_active() == screen2_get()) {
                    lv_screen_load_anim(screen1_get(), LV_SCR_LOAD_ANIM_MOVE_BOTTOM, 300, 0, false);
                } else {
                    /* If currently on screen3 or screen4, ignore up/down or go back to screen1 */
                    lv_screen_load_anim(screen1_get(), LV_SCR_LOAD_ANIM_MOVE_BOTTOM, 300, 0, false);
                }
            }
            else if (dir == LV_DIR_TOP) {
                if (lv_screen_active() == screen1_get()) {
                    lv_screen_load_anim(screen2_get(), LV_SCR_LOAD_ANIM_MOVE_TOP, 300, 0, false);
                } else if (lv_screen_active() == screen2_get()) {
                    lv_screen_load_anim(screen1_get(), LV_SCR_LOAD_ANIM_MOVE_TOP, 300, 0, false);
                } else {
                    lv_screen_load_anim(screen1_get(), LV_SCR_LOAD_ANIM_MOVE_TOP, 300, 0, false);
                }
            }
            else if (dir == LV_DIR_RIGHT) {
                /* Right swipe toggles between screen3 and screen4 */
                if (lv_screen_active() == screen3_get()) {
                    lv_screen_load_anim(screen4_get(), LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
                } else if (lv_screen_active() == screen4_get()) {
                    lv_screen_load_anim(screen3_get(), LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300, 0, false);
                } else {
                    /* From screen1 or screen2, enter screen3 via right swipe */
                    lv_screen_load_anim(screen3_get(), LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
                }
            }
            else if (dir == LV_DIR_LEFT) {
                /* Left swipe to return from sub-screens (3 or 4) to main cycle */
                if (lv_screen_active() == screen3_get() || lv_screen_active() == screen4_get()) {
                    lv_screen_load_anim(screen1_get(), LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300, 0, false);
                }
            }
        };

        lv_obj_add_event_cb(screen1_get(), gesture_cb, LV_EVENT_GESTURE, nullptr);
        lv_obj_add_event_cb(screen2_get(), gesture_cb, LV_EVENT_GESTURE, nullptr);
        lv_obj_add_event_cb(screen3_get(), gesture_cb, LV_EVENT_GESTURE, nullptr);
        lv_obj_add_event_cb(screen4_get(), gesture_cb, LV_EVENT_GESTURE, nullptr);

        // After some delay, we'll switch to screen2.
        // But for now, just stay on the current screen where s_label1/2 were created.
        bsp_display_unlock();
    }

    ESP_LOGI(TAG, "UI initialized");
}