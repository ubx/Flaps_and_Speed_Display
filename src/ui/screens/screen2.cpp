#ifndef NATIVE_TEST_BUILD

#include "esp_log.h"
#include "lvgl.h"
#include "../../flaputils.hpp"

// Waveshare BSP for this exact board (handles power rails + CO5300 + pins)
#include "bsp/esp32_s3_touch_amoled_1_75.h"

extern const lv_font_t digits_120;

extern flaputils::FlapSymbolResult get_flap_actual();

static lv_obj_t* s_screen = nullptr;
static lv_obj_t* s_flap_label = nullptr;
static lv_obj_t* s_scale = nullptr;
static lv_obj_t* s_triangle_canvas = nullptr;
static const char* s_flap_symbols[32];
static lv_style_t s_section_styles[32];

static void ui_update_timer_cb(lv_timer_t* /*t*/)
{
    if (lv_screen_active() != s_screen) return;

    if (s_flap_label)
    {
        flaputils::FlapSymbolResult res = get_flap_actual();
        lv_label_set_text(s_flap_label, res.symbol ? res.symbol : "-");
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
    lv_scale_set_angle_range(s_scale, 270);
    lv_scale_set_rotation(s_scale, 135);

    auto params = flaputils::get_flap_params();
    if (!params.empty())
    {
        uint32_t count = (uint32_t)params.size();
        // Set range 0 to count - 1 so symbols (ticks) are at integer positions 0, 1, 2...
        lv_scale_set_range(s_scale, 0, (int32_t)(count - 1));
        lv_scale_set_total_tick_count(s_scale, count);
        lv_scale_set_major_tick_every(s_scale, 1);
        lv_scale_set_label_show(s_scale, true);

        for (uint32_t i = 0; i < count && i < 31; ++i)
        {
            s_flap_symbols[i] = params[i].symbol;

            // Add sections BETWEEN symbols (gaps)
            if (i < count - 1)
            {
                lv_scale_section_t* section = lv_scale_add_section(s_scale);
                
                // Section covers the range from symbol i to symbol i+1
                lv_scale_set_section_range(s_scale, section, (int32_t)i, (int32_t)(i + 1));
                
                // Alternating colors for sections (gaps between symbols)
                lv_color_t section_color = (i % 2 == 0) ? lv_palette_main(LV_PALETTE_BLUE) : lv_palette_main(LV_PALETTE_CYAN);
                
                lv_style_init(&s_section_styles[i]);
                lv_style_set_line_color(&s_section_styles[i], section_color);
                lv_style_set_line_width(&s_section_styles[i], 10); // Thicker for sections (arc and ticks)
                
                lv_scale_set_section_style_indicator(s_scale, section, &s_section_styles[i]);
                lv_scale_set_section_style_main(s_scale, section, &s_section_styles[i]);
                lv_scale_set_section_style_items(s_scale, section, &s_section_styles[i]);
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
    s_triangle_canvas = lv_canvas_create(s_screen);
    lv_canvas_set_draw_buf(s_triangle_canvas, &draw_buf);
    lv_canvas_fill_bg(s_triangle_canvas, lv_color_black(), LV_OPA_0); // Transparent background

    lv_layer_t layer;
    lv_canvas_init_layer(s_triangle_canvas, &layer);

    lv_draw_triangle_dsc_t tri_dsc;
    lv_draw_triangle_dsc_init(&tri_dsc);
    // 70x70x70 equilateral triangle: height approx 60.6 pixels
    // Vertical center: (70 - 60.6) / 2 approx 4.7
    // Y coords: top point at 5, bottom points at 65 (total height 60)
    // X coords: center at 35, base points at 35 +/- 35 = 0 and 70
    tri_dsc.p[0].x = 35;
    tri_dsc.p[0].y = 5;
    tri_dsc.p[1].x = 0;
    tri_dsc.p[1].y = 65;
    tri_dsc.p[2].x = 70;
    tri_dsc.p[2].y = 65;
    tri_dsc.color = lv_color_white();
    tri_dsc.opa = LV_OPA_COVER;

    lv_draw_triangle(&layer, &tri_dsc);
    lv_canvas_finish_layer(s_triangle_canvas, &layer);

    lv_obj_align_to(s_triangle_canvas, s_flap_label, LV_ALIGN_OUT_TOP_MID, 0, -30);
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
