#include "ui.h"
#include "lvgl.h"
#include "screens/screen1.hpp"
#include "screens/screen2.hpp"
#include "screens/screen3.hpp"
#include "screens/screen4.hpp"
#include "../platform/ui_platform.hpp"

#ifdef NATIVE_SIMULATOR
#include <cstdio>
#define ESP_LOGI(tag, fmt, ...) std::printf("I (%s) " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGE(tag, fmt, ...) std::fprintf(stderr, "E (%s) " fmt "\n", tag, ##__VA_ARGS__)
#endif

static const char* TAG = "ui";
static lv_obj_t* s_label1 = nullptr;
static lv_obj_t* s_label2 = nullptr;
static lv_obj_t* s_label3 = nullptr;

void set_label1(const char* text)
{
    if (ui_platform_lock(-1))
    {
        if (s_label1) lv_label_set_text(s_label1, text);
        ui_platform_unlock();
    }
}

void set_label2(const char* text)
{
    if (ui_platform_lock(-1))
    {
        if (s_label2) lv_label_set_text(s_label2, text);
        ui_platform_unlock();
    }
}

void set_label3(const char* text)
{
    if (ui_platform_lock(-1))
    {
        if (s_label3) lv_label_set_text(s_label3, text);
        ui_platform_unlock();
    }
}

void ui_init()
{
    if (!ui_platform_init_display())
    {
        return;
    }

    if (ui_platform_lock(-1))
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
            constexpr uint32_t kLoadAnimDurationMs = 300;
            constexpr uint32_t kLoadAnimDelayMs = 0;

            if (dir == LV_DIR_BOTTOM) {
                if (lv_screen_active() == screen1_get()) {
                    lv_screen_load_anim(screen2_get(), LV_SCR_LOAD_ANIM_MOVE_BOTTOM, kLoadAnimDurationMs, kLoadAnimDelayMs, false);
                } else if (lv_screen_active() == screen2_get())
                    lv_screen_load_anim(screen1_get(), LV_SCR_LOAD_ANIM_MOVE_BOTTOM, kLoadAnimDurationMs, kLoadAnimDelayMs, false);
                else {
                    /* If currently on screen3 or screen4, ignore up/down or go back to screen1 */
                    lv_screen_load_anim(screen1_get(), LV_SCR_LOAD_ANIM_MOVE_BOTTOM, kLoadAnimDurationMs, kLoadAnimDelayMs, false);
                }
            }
            else if (dir == LV_DIR_TOP) {
                if (lv_screen_active() == screen1_get()) {
                    lv_screen_load_anim(screen2_get(), LV_SCR_LOAD_ANIM_MOVE_TOP, kLoadAnimDurationMs, kLoadAnimDelayMs, false);
                } else if (lv_screen_active() == screen2_get())
                    lv_screen_load_anim(screen1_get(), LV_SCR_LOAD_ANIM_MOVE_TOP, kLoadAnimDurationMs, kLoadAnimDelayMs, false);
                else {
                    lv_screen_load_anim(screen1_get(), LV_SCR_LOAD_ANIM_MOVE_TOP, kLoadAnimDurationMs, kLoadAnimDelayMs, false);
                }
            }
            else if (dir == LV_DIR_RIGHT) {
                /* Right swipe toggles between screen3 and screen4 */
                if (lv_screen_active() == screen3_get()) {
                    lv_screen_load_anim(screen4_get(), LV_SCR_LOAD_ANIM_MOVE_LEFT, kLoadAnimDurationMs, kLoadAnimDelayMs, false);
                } else if (lv_screen_active() == screen4_get()) {
                    lv_screen_load_anim(screen3_get(), LV_SCR_LOAD_ANIM_MOVE_RIGHT, kLoadAnimDurationMs, kLoadAnimDelayMs, false);
                } else {
                    /* From screen1 or screen2, enter screen3 via right swipe */
                    lv_screen_load_anim(screen3_get(), LV_SCR_LOAD_ANIM_MOVE_LEFT, kLoadAnimDurationMs, kLoadAnimDelayMs, false);
                }
            }
            else if (dir == LV_DIR_LEFT) {
                /* Left swipe to return from sub-screens (3 or 4) to the main cycle */
                if (lv_screen_active() == screen3_get() || lv_screen_active() == screen4_get()) {
                    lv_screen_load_anim(screen1_get(), LV_SCR_LOAD_ANIM_MOVE_RIGHT, kLoadAnimDurationMs, kLoadAnimDelayMs, false);
                }
            }
        };

        lv_obj_add_event_cb(screen1_get(), gesture_cb, LV_EVENT_GESTURE, nullptr);
        lv_obj_add_event_cb(screen2_get(), gesture_cb, LV_EVENT_GESTURE, nullptr);
        lv_obj_add_event_cb(screen3_get(), gesture_cb, LV_EVENT_GESTURE, nullptr);
        lv_obj_add_event_cb(screen4_get(), gesture_cb, LV_EVENT_GESTURE, nullptr);

        // After some delay, we'll switch to screen2.
        // But for now, just stay on the current screen where s_label1/2 was created.
        ui_platform_unlock();
    }
}
