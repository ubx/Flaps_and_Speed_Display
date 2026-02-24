#include "ui.h"
#include "lvgl.h"
#include "screens/screen1.hpp"
#include "screens/screen2.hpp"
#include "screens/screen3.hpp"
#include "esp_log.h"
#include "bsp/esp32_s3_touch_amoled_1_75.h"

static const char* TAG = "ui";

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
        screen1_create();
        screen2_create();
        screen3_create();

        /* Gestures:
           - UP/DOWN: cycle between screen1 <-> screen2 (as before)
           - RIGHT SWIPE: toggle screen3 (enter/exit)
           - screen3 is otherwise not in the up/down cycle
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
                    /* If currently on screen3, ignore up/down or go back to screen1 */
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
                /* Right swipe toggles screen3 */
                if (lv_screen_active() == screen3_get()) {
                    lv_screen_load_anim(screen1_get(), LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300, 0, false);
                } else {
                    lv_screen_load_anim(screen3_get(), LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
                }
            }
        };

        lv_obj_add_event_cb(screen1_get(), gesture_cb, LV_EVENT_GESTURE, nullptr);
        lv_obj_add_event_cb(screen2_get(), gesture_cb, LV_EVENT_GESTURE, nullptr);
        lv_obj_add_event_cb(screen3_get(), gesture_cb, LV_EVENT_GESTURE, nullptr);

        lv_screen_load(screen1_get());
        bsp_display_unlock();
    }

    ESP_LOGI(TAG, "UI initialized");
}