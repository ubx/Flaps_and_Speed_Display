#include "lvgl.h"
#include "../ui.h"
#include "../ui_helpers.hpp"
#include <cmath>
#include <cstdio>

/* ================= CONFIG ================= */

#define TAPE_WIDTH     160
#define TAPE_HEIGHT    400
#define PIXELS_PER_M   2.0f
#define MAJOR_STEP     100
#define MINOR_STEP     10

#define DIGIT_COUNT    5
#define DIGIT_WIDTH    80
#define DIGIT_HEIGHT   120
#define DIGIT_SPACING  4

/* ================= STATE ================= */

extern const lv_font_t mono_digits_120;
static lv_obj_t* s_screen = nullptr;
static lv_obj_t* s_tape = nullptr;
static lv_obj_t* s_center_box = nullptr;
static lv_obj_t* s_unit_label = nullptr;

static lv_obj_t* s_digit[DIGIT_COUNT] = {0};

static StaleOverlayState s_stale_overlay;

static float s_alt_filtered = 0;

static void tape_draw_event(lv_event_t* e)
{
    lv_obj_t* obj = lv_event_get_target_obj(e);
    lv_layer_t* layer = lv_event_get_layer(e);

    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);

    int center_y = (coords.y1 + coords.y2) / 2;

    float alt = s_alt_filtered;

    int base_alt = ((int)alt / MINOR_STEP) * MINOR_STEP;

    for (int a = base_alt - 500; a <= base_alt + 500; a += MINOR_STEP)
    {
        float dy = (alt - a) * PIXELS_PER_M;
        int y = center_y + (int)dy;

        if (y < coords.y1 || y > coords.y2) continue;

        bool major = (a % MAJOR_STEP == 0);
        int line_len = major ? 30 : 15;

        lv_draw_line_dsc_t line;
        lv_draw_line_dsc_init(&line);
        line.color = lv_color_white();
        line.width = major ? 3 : 1;

        line.p1.x = coords.x1 + 5;
        line.p1.y = y;
        line.p2.x = coords.x1 + 5 + line_len;
        line.p2.y = y;

        lv_draw_line(layer, &line);

        if (major)
        {
            char buf[16];
            snprintf(buf, sizeof(buf), "%d", a);

            lv_draw_label_dsc_t label;
            lv_draw_label_dsc_init(&label);
            label.color = lv_color_white();
            label.font = &lv_font_montserrat_20;

            lv_area_t txt_area = {
                coords.x1 + 40,
                y - 12,
                coords.x2,
                y + 12
            };

            label.text = buf;
            label.text_static = false;
            lv_draw_label(layer, &label, &txt_area);
        }
    }
}

/* ================= ALTITUDE ================= */

static void update_altitude(float alt)
{
    /* Low-pass filter */
    s_alt_filtered = 0.9f * s_alt_filtered + 0.1f * alt;

    int alt_int = (int)roundf(s_alt_filtered);

    /* Clamp */
    if (alt_int < 0) alt_int = 0;
    if (alt_int > 99999) alt_int = 99999;

    /* Leading zero blanking */
    bool leading = true;

    for (int i = 0; i < DIGIT_COUNT; i++)
    {
        int div = (int)pow(10, DIGIT_COUNT - 1 - i);
        int d = (alt_int / div) % 10;

        if (d != 0 || i == DIGIT_COUNT - 1)
            leading = false;

        if (s_digit[i])
        {
            if (leading)
            {
                lv_label_set_text(s_digit[i], " ");
            }
            else
            {
                char c[2] = { (char)('0' + d), '\0' };
                lv_label_set_text(s_digit[i], c);
            }
        }
    }

    if (s_unit_label)
    {
        lv_label_set_text(s_unit_label, "m");
    }

    lv_obj_invalidate(s_tape);
}

/* ================= UTILS ================= */

static inline void make_noninteractive(lv_obj_t* o)
{
    if (!o) return;
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
}

/* ================= TIMER ================= */

static void ui_update_timer_cb(lv_timer_t* timer)
{
    if (!s_screen || lv_screen_active() != s_screen) return;

    ui_set_stale_overlay(s_screen, s_stale_overlay, is_stale());
    update_altitude(get_alt_m());
}

/* ================= SCREEN ================= */

void screen5_create()
{
    s_screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(s_screen, lv_color_black(), 0);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    /* Tape */
    s_tape = lv_obj_create(s_screen);
    lv_obj_set_size(s_tape, TAPE_WIDTH, TAPE_HEIGHT);
    lv_obj_align(s_tape, LV_ALIGN_CENTER, 0, 0);

    lv_obj_set_style_bg_color(s_tape, lv_color_black(), 0);
    lv_obj_set_style_border_color(s_tape, lv_color_white(), 0);
    lv_obj_set_style_border_width(s_tape, 2, 0);

    lv_obj_add_event_cb(s_tape, tape_draw_event, LV_EVENT_DRAW_MAIN, NULL);

    /* Center box */
    s_center_box = lv_obj_create(s_screen);
    lv_obj_set_size(s_center_box, TAPE_WIDTH + 180, 140);
    lv_obj_align(s_center_box, LV_ALIGN_CENTER, 0, 0);

    lv_obj_set_style_bg_color(s_center_box, lv_color_black(), 0);
    lv_obj_set_style_border_color(s_center_box, lv_color_white(), 0);
    lv_obj_set_style_border_width(s_center_box, 3, 0);

    /* Digit container */
    lv_obj_t* digit_container = lv_obj_create(s_center_box);
    lv_obj_set_size(digit_container,
                    DIGIT_COUNT * DIGIT_WIDTH + (DIGIT_COUNT - 1) * DIGIT_SPACING,
                    DIGIT_HEIGHT);
    lv_obj_center(digit_container);

    lv_obj_set_style_bg_opa(digit_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(digit_container, 0, 0);
    lv_obj_clear_flag(digit_container, LV_OBJ_FLAG_SCROLLABLE);

    /* Create digits */
    for (int i = 0; i < DIGIT_COUNT; i++)
    {
        s_digit[i] = lv_label_create(digit_container);

        lv_obj_set_size(s_digit[i], DIGIT_WIDTH, DIGIT_HEIGHT);
        lv_obj_set_style_text_align(s_digit[i], LV_TEXT_ALIGN_CENTER, 0);

        lv_obj_set_style_text_font(s_digit[i], &mono_digits_120, 0);
        lv_obj_set_style_text_color(s_digit[i], lv_color_white(), 0);

        lv_label_set_text(s_digit[i], "0");

        lv_obj_align(s_digit[i], LV_ALIGN_LEFT_MID,
                     i * (DIGIT_WIDTH + DIGIT_SPACING), 0);
    }

    /* Unit label */
    s_unit_label = lv_label_create(s_screen);
    lv_obj_set_style_text_font(s_unit_label, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(s_unit_label, lv_color_white(), 0);
    lv_obj_align_to(s_unit_label, s_center_box, LV_ALIGN_OUT_RIGHT_BOTTOM, 5, -20);

    /* Title */
    lv_obj_t* title = lv_label_create(s_screen);
    make_noninteractive(title);
    lv_label_set_text(title, "Alt");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_align(title, LV_ALIGN_BOTTOM_MID, 0, -10);

    s_stale_overlay = {};

    lv_timer_create(ui_update_timer_cb, 33, NULL);
}

/* ================= GETTER ================= */

lv_obj_t* screen5_get()
{
    return s_screen;
}