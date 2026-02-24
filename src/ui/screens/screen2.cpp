#ifndef NATIVE_TEST_BUILD

#include "esp_log.h"
#include "lvgl.h"
#include "../../flaputils.hpp"

// Waveshare BSP for this exact board (handles power rails + CO5300 + pins)
#include "bsp/esp32_s3_touch_amoled_1_75.h"

extern const lv_font_t digits_120;

extern flaputils::FlapSymbolResult get_flap_actual();
extern flaputils::FlapSymbolResult get_flap_target();

static lv_obj_t* s_screen = nullptr;
static lv_obj_t* s_flap_label = nullptr;
static lv_obj_t* s_scale = nullptr;
static lv_obj_t* s_triangle_up_canvas = nullptr;
static lv_obj_t* s_triangle_down_canvas = nullptr;

static const char* s_flap_symbols[32];
static lv_style_t s_section_styles[32];

/* NEW: keep section pointers so we can toggle visibility */
static lv_scale_section_t* s_sections[32] = {nullptr};
static uint32_t s_section_count = 0;

/* NEW: hidden style for sections (arc transparent) */
static lv_style_t s_section_hidden_style;
static bool s_hidden_style_inited = false;

static int32_t s_last_target_idx = -9999;

static void ui_update_timer_cb(lv_timer_t* /*t*/)
{
    if (lv_screen_active() != s_screen) return;

    flaputils::FlapSymbolResult actual = get_flap_actual();
    flaputils::FlapSymbolResult target = get_flap_target();

    if (s_flap_label)
    {
        lv_label_set_text(s_flap_label, actual.symbol ? actual.symbol : "-");
    }

    if (s_triangle_up_canvas && s_triangle_down_canvas)
    {
        if (target.index > actual.index && target.index != -1 && actual.index != -1)
        {
            lv_obj_remove_flag(s_triangle_up_canvas, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_triangle_down_canvas, LV_OBJ_FLAG_HIDDEN);
        }
        else if (target.index < actual.index && target.index != -1 && actual.index != -1)
        {
            lv_obj_add_flag(s_triangle_up_canvas, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(s_triangle_down_canvas, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_add_flag(s_triangle_up_canvas, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_triangle_down_canvas, LV_OBJ_FLAG_HIDDEN);
        }
    }

    /* NEW: show ONLY the section with index == target.index */
    if (s_scale)
    {
        int32_t tgt = target.index;

        /* Clamp/validate against created sections: sections exist for i = 0..(count-2) */
        int32_t max_section = (int32_t)s_section_count - 2; /* because last tick has no gap section */

        /* If nothing changed, do nothing */
        if (tgt == s_last_target_idx) return;

        /* Hide previous section (if valid) */
        if (s_last_target_idx >= 0 && s_last_target_idx <= max_section)
        {
            uint32_t i = (uint32_t)s_last_target_idx;
            if (i < 31 && s_sections[i])
                lv_scale_section_set_style(s_sections[i], LV_PART_MAIN, &s_section_hidden_style);
        }

        /* Show new section (if valid) */
        if (tgt >= 0 && tgt <= max_section)
        {
            uint32_t i = (uint32_t)tgt;
            if (i < 31 && s_sections[i])
                lv_scale_section_set_style(s_sections[i], LV_PART_MAIN, &s_section_styles[i]);
        }

        s_last_target_idx = tgt;
    }
}

static void ui_create_screen2()
{
    s_screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(s_screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);

    // Dynamic Flap Value
    s_flap_label = lv_label_create(s_screen);
    lv_label_set_text(s_flap_label, "-");
    lv_obj_set_style_text_color(s_flap_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_flap_label, &digits_120, 0);
    lv_obj_center(s_flap_label);

    // Gauge (Scale)
    s_scale = lv_scale_create(s_screen);
    lv_obj_set_size(s_scale, 466, 466);
    lv_obj_center(s_scale);
    lv_scale_set_mode(s_scale, LV_SCALE_MODE_ROUND_INNER);
    lv_scale_set_draw_ticks_on_top(s_scale, true);
    lv_scale_set_label_show(s_scale, true);
    lv_obj_set_style_length(s_scale, 36, LV_PART_INDICATOR); /* major ticks */
    lv_scale_set_angle_range(s_scale, 270);
    lv_scale_set_rotation(s_scale, 135);

    auto params = flaputils::get_flap_params();
    if (!params.empty())
    {
        uint32_t count = (uint32_t)params.size();
        if (count < 2) count = 2; /* ensure at least one section possible */

        /* range 0..count-1 so ticks are integer positions */
        lv_scale_set_range(s_scale, 0, (int32_t)(count - 1));
        lv_scale_set_total_tick_count(s_scale, count);
        lv_scale_set_major_tick_every(s_scale, 1);
        lv_scale_set_label_show(s_scale, true);

        /* Init hidden style once */
        if (!s_hidden_style_inited)
        {
            s_hidden_style_inited = true;
            lv_style_init(&s_section_hidden_style);
            lv_style_set_arc_opa(&s_section_hidden_style, LV_OPA_0);
            lv_style_set_arc_width(&s_section_hidden_style, 30); /* match visible width */
        }

        /* We create sections for i = 0..count-2 */
        s_section_count = (count > 31) ? 31 : count;

        for (uint32_t i = 0; i < count && i < 31; ++i)
        {
            s_flap_symbols[i] = params[i].symbol;

            if (i < count - 1)
            {
                lv_scale_section_t* section = lv_scale_add_section(s_scale);
                s_sections[i] = section;

                /* LVGL v9: set section range */
                lv_scale_section_set_range(section, (int32_t)i, (int32_t)(i + 1));

                /* Visible style per section (alternating colors) */
                lv_color_t section_color =
                    (i % 2 == 0) ? lv_palette_main(LV_PALETTE_GREEN)
                                 : lv_palette_main(LV_PALETTE_YELLOW);

                lv_style_init(&s_section_styles[i]);
                lv_style_set_arc_color(&s_section_styles[i], section_color);
                lv_style_set_arc_width(&s_section_styles[i], 30);
                lv_style_set_arc_opa(&s_section_styles[i], LV_OPA_COVER);

                /* Start hidden; timer will show the correct one */
                lv_scale_section_set_style(section, LV_PART_MAIN, &s_section_hidden_style);
            }
            else
            {
                s_sections[i] = nullptr;
            }
        }

        s_flap_symbols[std::min(count, (uint32_t)31)] = nullptr;
        lv_scale_set_text_src(s_scale, s_flap_symbols);
    }

    lv_obj_set_style_text_color(s_scale, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_scale, &lv_font_montserrat_20, 0);

    lv_obj_set_style_line_color(s_scale, lv_color_white(), LV_PART_INDICATOR);
    lv_obj_set_style_line_width(s_scale, 4, LV_PART_INDICATOR);
    lv_obj_set_style_line_color(s_scale, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_line_width(s_scale, 4, LV_PART_MAIN);

    // Title
    lv_obj_t* title = lv_label_create(s_screen);
    lv_label_set_text(title, "Faps");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_align(title, LV_ALIGN_BOTTOM_MID, 0, -10);

    // Triangle above label
    LV_DRAW_BUF_DEFINE_STATIC(draw_buf, 70, 70, LV_COLOR_FORMAT_ARGB8888);
    LV_DRAW_BUF_INIT_STATIC(draw_buf);
    s_triangle_up_canvas = lv_canvas_create(s_screen);
    lv_canvas_set_draw_buf(s_triangle_up_canvas, &draw_buf);
    lv_canvas_fill_bg(s_triangle_up_canvas, lv_color_black(), LV_OPA_0); // Transparent background

    lv_layer_t layer;
    lv_canvas_init_layer(s_triangle_up_canvas, &layer);

    lv_draw_triangle_dsc_t tri_dsc;
    lv_draw_triangle_dsc_init(&tri_dsc);

    tri_dsc.p[0].x = 35;
    tri_dsc.p[0].y = 5;
    tri_dsc.p[1].x = 0;
    tri_dsc.p[1].y = 65;
    tri_dsc.p[2].x = 70;
    tri_dsc.p[2].y = 65;
    tri_dsc.color = lv_color_white();
    tri_dsc.opa = LV_OPA_COVER;

    lv_draw_triangle(&layer, &tri_dsc);
    lv_canvas_finish_layer(s_triangle_up_canvas, &layer);

    lv_obj_align_to(s_triangle_up_canvas, s_flap_label, LV_ALIGN_OUT_TOP_MID, 0, -40);

    // Triangle below label
    LV_DRAW_BUF_DEFINE_STATIC(draw_buf_down, 70, 70, LV_COLOR_FORMAT_ARGB8888);
    LV_DRAW_BUF_INIT_STATIC(draw_buf_down);
    s_triangle_down_canvas = lv_canvas_create(s_screen);
    lv_canvas_set_draw_buf(s_triangle_down_canvas, &draw_buf_down);
    lv_canvas_fill_bg(s_triangle_down_canvas, lv_color_black(), LV_OPA_0); // Transparent background

    lv_canvas_init_layer(s_triangle_down_canvas, &layer);

    // Pointing down:
    tri_dsc.p[0].x = 35;
    tri_dsc.p[0].y = 65;
    tri_dsc.p[1].x = 0;
    tri_dsc.p[1].y = 5;
    tri_dsc.p[2].x = 70;
    tri_dsc.p[2].y = 5;

    lv_draw_triangle(&layer, &tri_dsc);
    lv_canvas_finish_layer(s_triangle_down_canvas, &layer);

    lv_obj_align_to(s_triangle_down_canvas, s_flap_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 40);
}

void screen2_create()
{
    ui_create_screen2();
    lv_timer_create(ui_update_timer_cb, 200, nullptr);
}

lv_obj_t* screen2_get()
{
    return s_screen;
}

#else
void screen2_create()
{
}

lv_obj_t* screen2_get()
{
    return nullptr;
}
#endif