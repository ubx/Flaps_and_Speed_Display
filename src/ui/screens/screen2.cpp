#ifndef NATIVE_TEST_BUILD

#include "esp_log.h"
#include "lvgl.h"
#include "../../flaputils.hpp"
#include "bsp/esp32_s3_touch_amoled_1_75.h"
#include <cmath>

extern const lv_font_t digits_120;

extern flaputils::FlapSymbolResult get_flap_actual();
extern flaputils::FlapSymbolResult get_flap_target();
extern double get_weight_kg();

static lv_obj_t* s_screen = nullptr;
static lv_obj_t* s_flap_label = nullptr;
static lv_obj_t* s_scale = nullptr;                 // plain container (not lv_scale)
static lv_obj_t* s_triangle_up_canvas = nullptr;
static lv_obj_t* s_triangle_down_canvas = nullptr;

/* Arc ring segments */
static lv_obj_t* s_seg_arcs[32] = {nullptr};
static uint32_t  s_seg_count = 0;

/* Custom scale ticks + labels (so lengths match segments) */
static lv_obj_t*          s_tick_lines[33] = {nullptr};   // boundaries: 0..count
static lv_point_precise_t s_tick_pts[33][2];              // NOTE: must match lv_line_set_points signature
static lv_obj_t*          s_labels[32] = {nullptr};        // one per segment

/* Highlight bookkeeping */
static int32_t   s_last_highlight_idx = -9999;

/* Opacity levels for segments */
static const lv_opa_t SEG_OPA_DIM = LV_OPA_20;
static const lv_opa_t SEG_OPA_ON  = LV_OPA_COVER;

/* ---------- helpers ---------- */

static inline void make_noninteractive(lv_obj_t* o)
{
    if(!o) return;
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
}

static inline void make_screen_static(lv_obj_t* o)
{
    if(!o) return;
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
}

static inline int32_t fast_roundf(float x)
{
    return (int32_t)(x + (x >= 0.0f ? 0.5f : -0.5f));
}

static inline float deg2rad(float deg)
{
    return deg * (float)M_PI / 180.0f;
}

/* One segment per flap range => 0..count-1 */
static inline int32_t max_drawable_segment(void)
{
    if(s_seg_count < 1) return -1;
    return (int32_t)s_seg_count - 1;
}

static void set_target_segment(int32_t tgt)
{
    const int32_t max_seg = max_drawable_segment();
    if(max_seg < 0) return;

    /* Invalid => no highlight (dim previous only) */
    if(tgt < 0 || tgt > max_seg)
    {
        if(s_last_highlight_idx >= 0 && s_last_highlight_idx <= max_seg)
        {
            lv_obj_t* prev = s_seg_arcs[s_last_highlight_idx];
            if(prev) lv_obj_set_style_arc_opa(prev, SEG_OPA_DIM, LV_PART_INDICATOR);
        }
        s_last_highlight_idx = -1;
        return;
    }

    /* No change => do nothing */
    if(tgt == s_last_highlight_idx) return;

    /* First valid set: dim all once so only one becomes bright */
    if(s_last_highlight_idx == -9999)
    {
        for(int32_t i = 0; i <= max_seg && i < 31; i++) {
            if(s_seg_arcs[i]) lv_obj_set_style_arc_opa(s_seg_arcs[i], SEG_OPA_DIM, LV_PART_INDICATOR);
        }
    }
    else if(s_last_highlight_idx >= 0 && s_last_highlight_idx <= max_seg)
    {
        /* Dim previous */
        lv_obj_t* prev = s_seg_arcs[s_last_highlight_idx];
        if(prev) lv_obj_set_style_arc_opa(prev, SEG_OPA_DIM, LV_PART_INDICATOR);
    }

    /* Bright new */
    lv_obj_t* cur = s_seg_arcs[tgt];
    if(cur) lv_obj_set_style_arc_opa(cur, SEG_OPA_ON, LV_PART_INDICATOR);

    s_last_highlight_idx = tgt;
}

/* Try both LVGL transform APIs (v8/v9 variants) */
static inline void set_label_rotation_01deg(lv_obj_t* obj, int32_t angle01deg)
{
#if defined(LVGL_VERSION_MAJOR) && (LVGL_VERSION_MAJOR >= 9)
    /* LVGL 9 uses "transform_rotation" */
    lv_obj_set_style_transform_rotation(obj, angle01deg, 0);
#else
    /* LVGL 8 uses "transform_angle" */
    lv_obj_set_style_transform_angle(obj, angle01deg, 0);
#endif
}

/* Draw ticks at segment boundaries and labels at segment centers,
   using the SAME variable-length mapping as the arcs. */
static void draw_variable_scale(lv_obj_t* parent,
                                const std::vector<flaputils::FlapSpeedRange>& params,
                                uint32_t count,
                                const float* w,
                                float w_sum,
                                int32_t rot_deg,
                                int32_t span_deg,
                                int32_t gap_deg)
{
    if(!parent) return;

    const int32_t W  = lv_obj_get_width(parent);
    const int32_t H  = lv_obj_get_height(parent);
    const int32_t cx = W / 2;
    const int32_t cy = H / 2;

    /* Geometry */
    const int32_t tick_outer_r = (W / 2) - 4;
    const int32_t tick_inner_r = tick_outer_r - 14;
    const int32_t label_r      = tick_inner_r - 28;

    const int32_t tick_width   = 2;

    const int32_t usable_span  = span_deg - (int32_t)count * gap_deg;
    const float usable_span_f  = (usable_span > 0) ? (float)usable_span : (float)span_deg;

    /* ---- boundary ticks: i=0..count ---- */
    float acc = 0.0f;
    for(uint32_t i = 0; i <= count && i < 33; ++i)
    {
        float t = (w_sum > 0.0f) ? (acc / w_sum) : 0.0f;
        float a = t * usable_span_f;

        float boundary_deg = (float)rot_deg + a + (float)i * (float)gap_deg;

        float r = deg2rad(boundary_deg - 90.0f);
        int32_t x0 = cx + fast_roundf((float)tick_inner_r * cosf(r));
        int32_t y0 = cy + fast_roundf((float)tick_inner_r * sinf(r));
        int32_t x1 = cx + fast_roundf((float)tick_outer_r * cosf(r));
        int32_t y1 = cy + fast_roundf((float)tick_outer_r * sinf(r));

        s_tick_pts[i][0].x = x0;
        s_tick_pts[i][0].y = y0;
        s_tick_pts[i][1].x = x1;
        s_tick_pts[i][1].y = y1;

        lv_obj_t* line = s_tick_lines[i];
        if(!line)
        {
            line = lv_line_create(parent);
            s_tick_lines[i] = line;
            make_noninteractive(line);
            lv_obj_set_style_line_width(line, tick_width, 0);
            lv_obj_set_style_line_color(line, lv_color_white(), 0);
        }

        lv_line_set_points(line, s_tick_pts[i], 2);

        if(i < count) acc += w[i];
    }

    /* Hide any leftover tick lines from previous build */
    for(uint32_t i = count + 1; i < 33; ++i)
    {
        if(s_tick_lines[i]) lv_obj_add_flag(s_tick_lines[i], LV_OBJ_FLAG_HIDDEN);
    }
    for(uint32_t i = 0; i <= count && i < 33; ++i)
    {
        if(s_tick_lines[i]) lv_obj_remove_flag(s_tick_lines[i], LV_OBJ_FLAG_HIDDEN);
    }

    /* ---- labels at segment centers: i=0..count-1 ---- */
    acc = 0.0f;
    for(uint32_t i = 0; i < count && i < 32; ++i)
    {
        float a0 = (w_sum > 0.0f) ? ((acc / w_sum) * usable_span_f) : 0.0f;
        acc += w[i];
        float a1 = (w_sum > 0.0f) ? ((acc / w_sum) * usable_span_f) : 0.0f;

        float center_a  = 0.5f * (a0 + a1);
        float label_deg = (float)rot_deg + center_a + ((float)i + 0.5f) * (float)gap_deg;

        float r = deg2rad(label_deg - 90.0f);
        int32_t lx = cx + fast_roundf((float)label_r * cosf(r));
        int32_t ly = cy + fast_roundf((float)label_r * sinf(r));

        lv_obj_t* lab = s_labels[i];
        if(!lab)
        {
            lab = lv_label_create(parent);
            s_labels[i] = lab;
            make_noninteractive(lab);
            lv_obj_set_style_text_color(lab, lv_color_white(), 0);
            lv_obj_set_style_text_font(lab, &lv_font_montserrat_20, 0);
        }

        const char* sym = flaputils::get_range_symbol_name(params[i].index);
        lv_label_set_text(lab, sym ? sym : "");
        lv_obj_remove_flag(lab, LV_OBJ_FLAG_HIDDEN);

        /* Layout -> size */
        lv_obj_update_layout(lab);
        lv_coord_t lw = lv_obj_get_width(lab);
        lv_coord_t lh = lv_obj_get_height(lab);

        /* Position centered */
        lv_obj_set_pos(lab, (lv_coord_t)(lx - lw / 2), (lv_coord_t)(ly - lh / 2));

        /* Tangent rotation = radial + 90deg */
        float tang_deg = label_deg + 90.0f;

        /* Keep readable (avoid upside-down) */
        while(tang_deg < 0.0f) tang_deg += 360.0f;
        while(tang_deg >= 360.0f) tang_deg -= 360.0f;
        if(tang_deg > 90.0f && tang_deg < 270.0f) tang_deg += 180.0f;
        while(tang_deg >= 360.0f) tang_deg -= 360.0f;

        /* Pivot at label center */
        lv_obj_set_style_transform_pivot_x(lab, lw / 2, 0);
        lv_obj_set_style_transform_pivot_y(lab, lh / 2, 0);

        /* LVGL uses 0.1 degree units */
        set_label_rotation_01deg(lab, (int32_t)(tang_deg * 10.0f));
    }

    /* Hide leftover labels */
    for(uint32_t i = count; i < 32; ++i)
    {
        if(s_labels[i]) lv_obj_add_flag(s_labels[i], LV_OBJ_FLAG_HIDDEN);
    }
}

/* ---------- timer ---------- */

static void ui_update_timer_cb(lv_timer_t* /*t*/)
{
    if(lv_screen_active() != s_screen) return;

    flaputils::FlapSymbolResult actual = get_flap_actual();
    flaputils::FlapSymbolResult target = get_flap_target();

    if(s_flap_label) {
        const char* sym = flaputils::get_flap_symbol_name(actual.index);
        lv_label_set_text(s_flap_label, sym ? sym : "-");
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

    set_target_segment(target.index);
}

/* ---------- UI creation ---------- */

static void ui_create_screen2()
{
    s_screen = lv_obj_create(nullptr);
    make_screen_static(s_screen);
    lv_obj_set_style_bg_color(s_screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);

    /* Center value */
    s_flap_label = lv_label_create(s_screen);
    make_noninteractive(s_flap_label);
    lv_label_set_text(s_flap_label, "-");
    lv_obj_set_style_text_color(s_flap_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_flap_label, &digits_120, 0);
    lv_obj_center(s_flap_label);

    /* Scale container (custom ticks/labels + arcs) */
    s_scale = lv_obj_create(s_screen);
    make_noninteractive(s_scale);
    lv_obj_set_size(s_scale, 466, 466);
    lv_obj_center(s_scale);
    lv_obj_set_style_bg_opa(s_scale, LV_OPA_0, 0);
    lv_obj_set_style_border_width(s_scale, 0, 0);
    lv_obj_set_style_pad_all(s_scale, 0, 0);

    /* Build labels + segments */
    auto params = flaputils::get_flap_speed_ranges(get_weight_kg());
    if(!params.empty())
    {
        uint32_t count = (uint32_t)params.size();
        if(count > 31) count = 31;

        s_seg_count = count;
        s_last_highlight_idx = -9999;

        const int32_t rot  = 135;
        const int32_t span = 270;

        const int32_t ring_w    = 20;
        const int32_t ring_size = 430;

        for(uint32_t i = 0; i < 32; i++) s_seg_arcs[i] = nullptr;

        /* Compute weights */
        float w_sum = 0.0f;
        float w[32] = {0};

        for(uint32_t i = 0; i < count; ++i)
        {
            float lo = params[i].lower_speed;
            float hi = params[i].upper_speed;

            float wi = 0.0f;
            if(lo >= 0.0f && hi >= 0.0f && hi > lo)
                wi = hi - lo;
            else
                wi = 0.0f;

            w[i] = wi;
            w_sum += wi;
        }

        /* Fallback: if everything is NA/zero, distribute equally */
        if(w_sum <= 0.0f)
        {
            for(uint32_t i = 0; i < count; ++i) w[i] = 1.0f;
            w_sum = (float)count;
        }

        const int32_t gap_deg = 2;

        const int32_t usable_span = span - (int32_t)count * gap_deg;
        const float usable_span_f = (usable_span > 0) ? (float)usable_span : (float)span;

        float acc = 0.0f;

        for(uint32_t i = 0; i < count; ++i)
        {
            if(i >= 31) break;

            lv_obj_t* arc = lv_arc_create(s_scale);
            s_seg_arcs[i] = arc;
            make_noninteractive(arc);

            lv_obj_set_size(arc, ring_size, ring_size);
            lv_obj_align(arc, LV_ALIGN_CENTER, 0, 0);

            lv_obj_remove_style(arc, nullptr, LV_PART_KNOB);

            lv_obj_set_style_arc_width(arc, ring_w, LV_PART_INDICATOR);
            lv_obj_set_style_arc_width(arc, ring_w, LV_PART_MAIN);
            lv_obj_set_style_arc_opa(arc, LV_OPA_0, LV_PART_MAIN);

            lv_obj_set_style_arc_color(arc, lv_palette_main(LV_PALETTE_GREEN), LV_PART_INDICATOR);

            lv_arc_set_rotation(arc, (int16_t)rot);

            float a0f = (acc / w_sum) * usable_span_f;
            acc += w[i];
            float a1f = (acc / w_sum) * usable_span_f;

            int32_t a0 = fast_roundf(a0f) + (int32_t)i * gap_deg;
            int32_t a1 = fast_roundf(a1f) + (int32_t)i * gap_deg;

            if(a1 <= a0) a1 = a0 + 1;

            lv_arc_set_angles(arc, (int16_t)a0, (int16_t)a1);

            lv_obj_set_style_arc_opa(arc, SEG_OPA_DIM, LV_PART_INDICATOR);

            lv_obj_move_background(arc);
        }

        /* Ticks + tangent-rotated labels (scale matches segment lengths) */
        draw_variable_scale(s_scale, params, count, w, w_sum, rot, span, gap_deg);
    }

    /* Title */
    lv_obj_t* title = lv_label_create(s_screen);
    make_noninteractive(title);
    lv_label_set_text(title, "Flaps");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_align(title, LV_ALIGN_BOTTOM_MID, 0, -10);

    /* Triangle above label */
    LV_DRAW_BUF_DEFINE_STATIC(draw_buf, 70, 70, LV_COLOR_FORMAT_ARGB8888);
    LV_DRAW_BUF_INIT_STATIC(draw_buf);
    s_triangle_up_canvas = lv_canvas_create(s_screen);
    make_noninteractive(s_triangle_up_canvas);
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
    make_noninteractive(s_triangle_down_canvas);
    lv_canvas_set_draw_buf(s_triangle_down_canvas, &draw_buf_down);
    lv_canvas_fill_bg(s_triangle_down_canvas, lv_color_black(), LV_OPA_0);

    lv_canvas_init_layer(s_triangle_down_canvas, &layer);

    tri_dsc.p[0].x = 35; tri_dsc.p[0].y = 65;
    tri_dsc.p[1].x = 0;  tri_dsc.p[1].y = 5;
    tri_dsc.p[2].x = 70; tri_dsc.p[2].y = 5;

    lv_draw_triangle(&layer, &tri_dsc);
    lv_canvas_finish_layer(s_triangle_down_canvas, &layer);
    lv_obj_align_to(s_triangle_down_canvas, s_flap_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 40);

    lv_obj_add_flag(s_triangle_up_canvas, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_triangle_down_canvas, LV_OBJ_FLAG_HIDDEN);
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