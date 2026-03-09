#include "ui_platform.hpp"

#include <algorithm>

#ifdef NATIVE_SIMULATOR
#include <mutex>
#include "drivers/sdl/lv_sdl_keyboard.h"
#include "drivers/sdl/lv_sdl_mouse.h"
#include "drivers/sdl/lv_sdl_mousewheel.h"
#include "drivers/sdl/lv_sdl_window.h"

namespace
{
std::recursive_mutex g_lvgl_mutex;
lv_display_t* g_display = nullptr;
lv_indev_t* g_mouse = nullptr;
lv_indev_t* g_mousewheel = nullptr;
lv_indev_t* g_keyboard = nullptr;
int g_brightness_percent = 100;
}

bool ui_platform_init_display()
{
    if (g_display) return true;

    lv_init();

    g_display = lv_sdl_window_create(466, 466);
    if (!g_display) return false;

    lv_sdl_window_set_title(g_display, "Flaps & Speed Simulator");
    lv_sdl_window_set_resizeable(g_display, false);
    g_mouse = lv_sdl_mouse_create();
    g_mousewheel = lv_sdl_mousewheel_create();
    g_keyboard = lv_sdl_keyboard_create();
    return g_mouse && g_mousewheel && g_keyboard;
}

bool ui_platform_lock(int)
{
    g_lvgl_mutex.lock();
    return true;
}

void ui_platform_unlock()
{
    g_lvgl_mutex.unlock();
}

int ui_platform_get_brightness()
{
    return g_brightness_percent;
}

void ui_platform_set_brightness(int brightness_percent)
{
    g_brightness_percent = std::clamp(brightness_percent, 0, 100);
}

lv_display_t* ui_platform_get_display()
{
    return g_display;
}
#else
extern "C" {
#include "bsp/esp32_s3_touch_amoled_1_75.h"
}

bool ui_platform_init_display()
{
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
    return bsp_display_start_with_config(&cfg) != nullptr;
}

bool ui_platform_lock(int timeout_ms)
{
    return bsp_display_lock(timeout_ms) == ESP_OK;
}

void ui_platform_unlock()
{
    bsp_display_unlock();
}

int ui_platform_get_brightness()
{
    return bsp_display_brightness_get();
}

void ui_platform_set_brightness(int brightness_percent)
{
    bsp_display_brightness_set(brightness_percent);
}

lv_display_t* ui_platform_get_display()
{
    return lv_display_get_default();
}
#endif
