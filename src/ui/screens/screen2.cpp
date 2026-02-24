#ifndef NATIVE_TEST_BUILD

#include "esp_log.h"
#include "lvgl.h"
#include "../../flaputils.hpp"
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

/* Arc ring segments (one per gap i..i+1, but we will SKIP last->first) */
static lv_obj_t* s_seg_arcs[32] = {nullptr};
static uint32_t  s_seg_count = 0;
static int32_t   s_last_target_idx = -9999;

/* Styles for segments */
static lv_style_t s_seg_style_visible;
static lv_style_t s_seg_style_dim;
static bool s_seg_styles_inited = false;

/* We do NOT draw the “wrap-around” gap between last and first:
   - means: we only create segments for i = 0..count-2 (already true)
   - and we also never highlight any wrap-around “direction” case
*/
static inline int32_t max_drawable_segment(void)
{
    if(s_seg_count < 2) return -1;
    return (int32_t)s_seg_count - 2; /* segments exist for gaps 0..count-2 */
}

static void set_target_segment(int32_t tgt)
{
    const int32_t max_seg = max_drawable_segment();
    if(max_seg < 0) return;

    /* If target is invalid or points to the last tick (no gap), show nothing (dim all) */
    if(tgt < 0 || tgt > max_seg)
    {
        for(uint32_t i = 0; i < (uint32_t)(max_seg + 1) && i < 31; i++) {
            if(s_seg_arcs[i]) lv_obj_add_style(s_seg_arcs[i], &s_seg_style_dim, LV_PART_INDICATOR);
        }
        s_last_target_idx = tgt;
        return;
    }

    if(tgt == s_last_target_idx) return;

    /* Dim previous */
    if(s_last_target_idx >= 0 && s_last_target_idx <= max_seg)
    {
        uint32_t pi = (uint32_t)s_last_target_idx;
        if(pi < 31 && s_seg_arcs[pi]) {
            lv_obj_add_style(s_seg_arcs[pi], &s_seg_style_dim, LV_PART_INDICATOR);
        }
    }

    /* Dim all once on first valid set (so only one is bright) */
    if(s_last_target_idx == -9999)
    {
        for(uint32_t i = 0; i < (uint32_t)(max_seg + 1) && i < 31; i++) {
            if(s_seg_arcs[i]) lv_obj_add_style(s_seg_arcs[i], &s_seg_style_dim, LV_PART_INDICATOR);
        }
    }

    /* Bright new */
    uint32_t ni = (uint32_t)tgt;
    if(ni < 31 && s_seg_arcs[ni]) {
        lv_obj_add_style(s_seg_arcs[ni], &s_seg_style_visible, LV_PART_INDICATOR);
    }

    s_last_target_idx = tgt;
}

static void ui_update_timer_cb(lv_timer_t* /*t*/)
{
    if(lv_screen_active() != s_screen) return;

    flaputils::FlapSymbolResult actual = get_flap_actual();
    flaputils::FlapSymbolResult target = get_flap_target();

    if(s_flap_label) {
        lv_label_set_text(s_flap_label, actual.symbol ? actual.symbol : "-");
    }

    if(s_triangle_up_canvas && s_triangle_down_canvas)
    {
        if(target.index > actual.index && target.index != -1 && actual.index != -1)
        {
            lv_obj_remove_flag(s_triangle_up_canvas, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_triangle_down_canvas, LV_OBJ_FLAG_HIDDEN);
        }
        else if(target.index < actual.index && target.index != -1 && actual.index != -1)
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

    /* Highlight only the target gap segment; no wrap-around (last<->first) segment exists */
    set_target_segment(target.index);
}

static void ui_create_screen2()
{
    s_screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(s_screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);

    /* Big center label */
    s_flap_label = lv_label_create(s_screen);
    lv_label_set_text(s_flap_label, "-");
    lv_obj_set_style_text_color(s_flap_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_flap_label, &digits_120, 0);
    lv_obj_center(s_flap_label);

    /* Create scale (ticks + labels only) */
    s_scale = lv_scale_create(s_screen);
    lv_obj_set_size(s_scale, 466, 466);
    lv_obj_center(s_scale);

    lv_scale_set_mode(s_scale, LV_SCALE_MODE_ROUND_INNER);
    lv_scale_set_draw_ticks_on_top(s_scale, true);
    lv_scale_set_label_show(s_scale, true);

    lv_obj_set_style_length(s_scale, 36, LV_PART_INDICATOR); /* major ticks */
    lv_obj_set_style_length(s_scale, 14, LV_PART_ITEMS);     /* minor ticks */

    lv_scale_set_angle_range(s_scale, 270);
    lv_scale_set_rotation(s_scale, 135);

    /* Base tick/label style */
    lv_obj_set_style_text_color(s_scale, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_scale, &lv_font_montserrat_20, 0);
    lv_obj_set_style_line_color(s_scale, lv_color_white(), LV_PART_INDICATOR);
    lv_obj_set_style_line_width(s_scale, 4, LV_PART_INDICATOR);
    lv_obj_set_style_line_color(s_scale, lv_color_white(), LV_PART_ITEMS);
    lv_obj_set_style_line_width(s_scale, 2, LV_PART_ITEMS);

    /* Don’t draw any ring from scale itself */
    lv_obj_set_style_line_width(s_scale, 0, LV_PART_MAIN);

    /* Init segment styles once */
    if(!s_seg_styles_inited)
    {
        s_seg_styles_inited = true;

        lv_style_init(&s_seg_style_visible);
        lv_style_set_arc_opa(&s_seg_style_visible, LV_OPA_COVER);

        lv_style_init(&s_seg_style_dim);
        lv_style_set_arc_opa(&s_seg_style_dim, LV_OPA_20);
    }

    auto params = flaputils::get_flap_params();
    if(!params.empty())
    {
        uint32_t count = (uint32_t)params.size();
        if(count > 31) count = 31;

        s_seg_count = count;

        lv_scale_set_range(s_scale, 0, (int32_t)(count - 1));
        lv_scale_set_total_tick_count(s_scale, count);
        lv_scale_set_major_tick_every(s_scale, 1);
        lv_scale_set_label_show(s_scale, true);

        for(uint32_t i = 0; i < count; ++i) {
            s_flap_symbols[i] = params[i].symbol;
        }
        s_flap_symbols[count] = nullptr;
        lv_scale_set_text_src(s_scale, s_flap_symbols);

        /* Create arc segments ONLY for gaps i..i+1 (i=0..count-2).
           That means: NO segment between last and first. */
        const int32_t rot  = 135;
        const int32_t span = 270;

        const int32_t ring_w    = 20;
        const int32_t ring_size = 430;

        for(uint32_t i = 0; i < 32; i++) s_seg_arcs[i] = nullptr;

        for(uint32_t i = 0; i < count - 1; i++)
        {
            lv_obj_t* arc = lv_arc_create(s_scale);
            s_seg_arcs[i] = arc;

            lv_obj_set_size(arc, ring_size, ring_size);
            lv_obj_align(arc, LV_ALIGN_CENTER, 0, 0);

            /* Pure ring segment: no knob */
            lv_obj_remove_style(arc, nullptr, LV_PART_KNOB);

            lv_obj_set_style_arc_width(arc, ring_w, LV_PART_INDICATOR);
            lv_obj_set_style_arc_width(arc, ring_w, LV_PART_MAIN);

            /* Background arc transparent */
            lv_obj_set_style_arc_opa(arc, LV_OPA_0, LV_PART_MAIN);

            /* Color (your example uses all green; keep as-is or alternate) */
            lv_color_t c = lv_palette_main(LV_PALETTE_GREEN);
            lv_obj_set_style_arc_color(arc, c, LV_PART_INDICATOR);

            /* Segment angles for gap i..i+1 */
            float a0 = (float)rot + ((float)span * (float)i)       / (float)(count - 1);
            float a1 = (float)rot + ((float)span * (float)(i + 1)) / (float)(count - 1);

            lv_arc_set_bg_angles(arc, 0, 360);
            lv_arc_set_angles(arc, (int16_t)a0, (int16_t)a1);
            lv_arc_set_rotation(arc, 0);

            /* Start dim */
            lv_obj_add_style(arc, &s_seg_style_dim, LV_PART_INDICATOR);

            /* Keep arcs behind ticks/labels */
            lv_obj_move_background(arc);
        }
    }

    /* Title */
    lv_obj_t* title = lv_label_create(s_screen);
    lv_label_set_text(title, "Faps");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_align(title, LV_ALIGN_BOTTOM_MID, 0, -10);

    /* Triangle above label */
    LV_DRAW_BUF_DEFINE_STATIC(draw_buf, 70, 70, LV_COLOR_FORMAT_ARGB8888);
    LV_DRAW_BUF_INIT_STATIC(draw_buf);
    s_triangle_up_canvas = lv_canvas_create(s_screen);
    lv_canvas_set_draw_buf(s_triangle_up_canvas, &draw_buf);
    lv_canvas_fill_bg(s_triangle_up_canvas, lv_color_black(), LV_OPA_0);

    lv_layer_t layer;
    lv_canvas_init_layer(s_triangle_up_canvas, &layer);

    lv_draw_triangle_dsc_t tri_dsc;
    lv_draw_triangle_dsc_init(&tri_dsc);
    tri_dsc.p[0].x = 35; tri_dsc.p[0].y = 5;
    tri_dsc.p[1].x = 0;  tri_dsc.p[1].y = 65;
    tri_dsc.p[2].x = 70; tri_dsc.p[2].y = 65;
    tri_dsc.color = lv_color_white();
    tri_dsc.opa = LV_OPA_COVER;

    lv_draw_triangle(&layer, &tri_dsc);
    lv_canvas_finish_layer(s_triangle_up_canvas, &layer);
    lv_obj_align_to(s_triangle_up_canvas, s_flap_label, LV_ALIGN_OUT_TOP_MID, 0, -40);

    /* Triangle below label */
    LV_DRAW_BUF_DEFINE_STATIC(draw_buf_down, 70, 70, LV_COLOR_FORMAT_ARGB8888);
    LV_DRAW_BUF_INIT_STATIC(draw_buf_down);
    s_triangle_down_canvas = lv_canvas_create(s_screen);
    lv_canvas_set_draw_buf(s_triangle_down_canvas, &draw_buf_down);
    lv_canvas_fill_bg(s_triangle_down_canvas, lv_color_black(), LV_OPA_0);

    lv_canvas_init_layer(s_triangle_down_canvas, &layer);

    tri_dsc.p[0].x = 35; tri_dsc.p[0].y = 65;
    tri_dsc.p[1].x = 0;  tri_dsc.p[1].y = 5;
    tri_dsc.p[2].x = 70; tri_dsc.p[2].y = 5;

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
void screen2_create() {}
lv_obj_t* screen2_get() { return nullptr; }
#endif