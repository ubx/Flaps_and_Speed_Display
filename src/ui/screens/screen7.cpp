#include "lvgl.h"
#include "../ui.h"
#include "../ui_helpers.hpp"
#include "../../flaputils.hpp"
#include <dirent.h>
#include <string>
#include <vector>

static lv_obj_t* s_screen = nullptr;
static lv_obj_t* s_roller = nullptr;

static std::string get_spiffs_file_list()
{
    std::string file_list;
#ifdef NATIVE_TEST_BUILD
    const char* path = "spiffs_data";
#else
    const char* path = "/spiffs";
#endif
    DIR* dir = opendir(path);
    if (dir == nullptr) {
        return "No SPIFFS";
    }

    struct dirent* ent;
    while ((ent = readdir(dir)) != nullptr) {
        if (ent->d_name[0] == '.') continue;
        if (ent->d_type == DT_REG) {
            if (!file_list.empty()) {
                file_list += "\n";
            }
            file_list += ent->d_name;
        }
    }
    closedir(dir);

    if (file_list.empty()) {
        return "Empty";
    }
    return file_list;
}

static void select_event_cb(lv_event_t* e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        char buf[256];
        lv_roller_get_selected_str(s_roller, buf, sizeof(buf));
        std::string filename = buf;
        if (filename != "No SPIFFS" && filename != "Empty") {
            std::string path;
#ifdef NATIVE_TEST_BUILD
            path = "spiffs_data/";
#else
            path = "/spiffs/";
#endif
            path += filename;
            flaputils::load_data(path.c_str());
        }
    }
}

static void ui_create_polar()
{
    s_screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(s_screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);

    /* Title */
    lv_obj_t* title = lv_label_create(s_screen);
    lv_label_set_text(title, "Polar Files");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_width(title, LV_PCT(100));
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(title, LV_ALIGN_BOTTOM_MID, 0, -10);

    /* Roller */
    s_roller = lv_roller_create(s_screen);
    std::string files = get_spiffs_file_list();
    lv_roller_set_options(s_roller, files.c_str(), LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(s_roller, 4);
    lv_obj_set_width(s_roller, 200);
    lv_obj_align(s_roller, LV_ALIGN_CENTER, 0, -20);
    lv_obj_set_style_text_font(s_roller, &lv_font_montserrat_16, 0);
    lv_obj_set_style_bg_color(s_roller, lv_color_hex(0x333333), 0);
    lv_obj_set_style_text_color(s_roller, lv_color_white(), 0);
    lv_obj_set_style_bg_color(s_roller, lv_color_hex(0x0078D7), LV_PART_SELECTED);

    /* Select Button */
    lv_obj_t* btn = lv_button_create(s_screen);
    lv_obj_set_size(btn, 100, 40);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -100);
    lv_obj_add_event_cb(btn, select_event_cb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* label = lv_label_create(btn);
    lv_label_set_text(label, "Select");
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
