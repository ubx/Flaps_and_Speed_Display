#include "lvgl.h"
#include "../ui.h"
#include <cmath>
#include <cstdio>

/* ================= CONFIG ================= */

#define TAPE_WIDTH     140
#define TAPE_HEIGHT    400
#define PIXELS_PER_M   2.0f     // scaling (adjust!)
#define MAJOR_STEP     100      // major tick (m)
#define MINOR_STEP     10       // minor tick (m)

/* ================= STATE ================= */

static lv_obj_t* s_screen = nullptr;
static lv_obj_t* s_tape = nullptr;
static lv_obj_t* s_center_box = nullptr;
static lv_obj_t* s_alt_label = nullptr;
static lv_obj_t* s_unit_label = nullptr;
static lv_obj_t* s_stale_cross_a = nullptr;
static lv_obj_t* s_stale_cross_b = nullptr;
static bool s_stale_overlay_visible = false;

static float s_alt_filtered = 0;

/* ================= DRAW EVENT ================= */

static void tape_draw_event(lv_event_t* e)
{
    lv_obj_t* obj = lv_event_get_target_obj(e);
    lv_layer_t* layer = lv_event_get_layer(e);

    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);

    int center_y = (coords.y1 + coords.y2) / 2;

    float alt = s_alt_filtered;

    /* find base altitude aligned to MINOR_STEP */
    int base_alt = ((int)alt / MINOR_STEP) * MINOR_STEP;

    for (int a = base_alt - 500; a <= base_alt + 500; a += MINOR_STEP)
    {
        float dy = (alt - a) * PIXELS_PER_M;
        int y = center_y + (int)dy;

        if (y < coords.y1 || y > coords.y2) continue;

        bool major = (a % MAJOR_STEP == 0);

        int line_len = major ? 30 : 15;

        /* draw tick */
        lv_draw_line_dsc_t line;
        lv_draw_line_dsc_init(&line);
        line.color = lv_color_white();
        line.width = major ? 3 : 1;

        line.p1.x = coords.x1 + 5;
        line.p1.y = y;
        line.p2.x = coords.x1 + 5 + line_len;
        line.p2.y = y;

        lv_draw_line(layer, &line);

        /* draw label */
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
    /* low-pass filter */
    s_alt_filtered = 0.9f * s_alt_filtered + 0.1f * alt;

    if (s_alt_label)
    {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.0f", s_alt_filtered);
        lv_label_set_text(s_alt_label, buf);
    }
    if (s_unit_label)
    {
        lv_label_set_text(s_unit_label, "m");
    }

    lv_obj_invalidate(s_tape);
}

/* ================= STALE OVERLAY ================= */

static void ui_set_stale_overlay(bool show)
{
    if (show == s_stale_overlay_visible) return;
    s_stale_overlay_visible = show;

    if (show)
    {
        if (!s_stale_cross_a)
        {
            static lv_point_precise_t cross_a_pts[2] = {{60, 60}, {406, 406}};
            s_stale_cross_a = lv_line_create(s_screen);
            lv_line_set_points(s_stale_cross_a, cross_a_pts, 2);
            lv_obj_set_style_line_width(s_stale_cross_a, 10, 0);
            lv_obj_set_style_line_color(s_stale_cross_a, lv_palette_main(LV_PALETTE_RED), 0);
            lv_obj_set_style_line_rounded(s_stale_cross_a, true, 0);
        }
        lv_obj_remove_flag(s_stale_cross_a, LV_OBJ_FLAG_HIDDEN);
        if (!s_stale_cross_b)
        {
            static lv_point_precise_t cross_b_pts[2] = {{406, 60}, {60, 406}};
            s_stale_cross_b = lv_line_create(s_screen);
            lv_line_set_points(s_stale_cross_b, cross_b_pts, 2);
            lv_obj_set_style_line_width(s_stale_cross_b, 10, 0);
            lv_obj_set_style_line_color(s_stale_cross_b, lv_palette_main(LV_PALETTE_RED), 0);
            lv_obj_set_style_line_rounded(s_stale_cross_b, true, 0);
        }
        lv_obj_remove_flag(s_stale_cross_b, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    if (s_stale_cross_a) lv_obj_add_flag(s_stale_cross_a, LV_OBJ_FLAG_HIDDEN);
    if (s_stale_cross_b) lv_obj_add_flag(s_stale_cross_b, LV_OBJ_FLAG_HIDDEN);
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

    ui_set_stale_overlay(is_stale());
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

    /* Center box (current altitude) */
    s_center_box = lv_obj_create(s_screen);
    lv_obj_set_size(s_center_box, TAPE_WIDTH + 140, 140);
    lv_obj_align(s_center_box, LV_ALIGN_CENTER, 0, 0);

    lv_obj_set_style_bg_color(s_center_box, lv_color_black(), 0);
    lv_obj_set_style_border_color(s_center_box, lv_color_white(), 0);
    lv_obj_set_style_border_width(s_center_box, 3, 0);

    /* Altitude text */
    extern const lv_font_t digits_120;
    LV_FONT_DECLARE(lv_font_montserrat_28);

    s_alt_label = lv_label_create(s_center_box);
    lv_obj_set_style_text_font(s_alt_label, &digits_120, 0);
    lv_obj_set_style_text_color(s_alt_label, lv_color_white(), 0);
    lv_obj_align(s_alt_label, LV_ALIGN_CENTER, -20, 0);

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

    /* store label pointer via user data if needed */

    lv_timer_create(ui_update_timer_cb, 33, NULL);
}

/* ================= GETTER ================= */

lv_obj_t* screen5_get()
{
    return s_screen;
}