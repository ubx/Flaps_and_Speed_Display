#include "lvgl.h"
#include "../ui.h"
#include "../ui_helpers.hpp"
#include <dirent.h>
#include <string>
#include <vector>

static lv_obj_t* s_screen = nullptr;

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
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    /* Roller */
    lv_obj_t* roller = lv_roller_create(s_screen);
    std::string files = get_spiffs_file_list();
    lv_roller_set_options(roller, files.c_str(), LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(roller, 3);
    lv_obj_set_width(roller, 200);
    lv_obj_center(roller);
    lv_obj_set_style_text_font(roller, &lv_font_montserrat_16, 0);
    lv_obj_set_style_bg_color(roller, lv_color_hex(0x333333), 0);
    lv_obj_set_style_text_color(roller, lv_color_white(), 0);
    lv_obj_set_style_bg_color(roller, lv_color_hex(0x0078D7), LV_PART_SELECTED);
}

void screen7_create()
{
    ui_create_polar();
}

lv_obj_t* screen7_get()
{
    return s_screen;
}
