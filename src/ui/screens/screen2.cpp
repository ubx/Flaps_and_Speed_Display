#ifndef NATIVE_TEST_BUILD

#include "esp_log.h"
#include "esp_task_wdt.h"
#include "lvgl.h"
#include "../../flaputils.hpp"
#include "bsp/esp32_s3_touch_amoled_1_75.h"
#include <cmath>
#include <cstdint>
#include <vector>

extern const lv_font_t digits_120;

extern float get_ias_kmh();
extern flaputils::FlapSymbolResult get_flap_actual();
extern flaputils::FlapSymbolResult get_flap_target();
extern double get_weight_kg();

static lv_obj_t* s_screen = nullptr;
static lv_obj_t* s_flap_label = nullptr;
static lv_obj_t* s_arc_container = nullptr; // plain container (not lv_scale)
static lv_obj_t* s_scale = nullptr;         // lv_scale for needle
static lv_obj_t* s_needle = nullptr;
static lv_obj_t* s_triangle_up_canvas = nullptr;
static lv_obj_t* s_triangle_down_canvas = nullptr;
static bool s_initialized = false;
static double s_last_weight = -1.0;

/* Needle dimensions */
static constexpr int32_t NEEDLE_INNER_RADIUS = 130;
static constexpr int32_t NEEDLE_OUTER_RADIUS = 170;

/* IAS range */
static constexpr float ASI_MIN = 40.0f;
static constexpr float ASI_MAX = 280.0f;

/* Arc ring segments */
static lv_obj_t* s_seg_arcs[32] = {nullptr};
static uint32_t s_seg_count = 0;

/* Custom scale ticks + labels (so lengths match segments) */
static lv_obj_t* s_tick_lines[33] = {nullptr};      // boundaries: 0..count
static lv_point_precise_t s_tick_pts[33][2];        // NOTE: must match lv_line_set_points signature
static lv_obj_t* s_labels[32] = {nullptr};          // one per segment

/* Highlight bookkeeping */
static int32_t s_last_highlight_idx = -9999;
static int32_t s_last_actual_idx = -9999;
static int32_t s_last_target_idx = -9999;

/* Opacity levels for segments */
static const lv_opa_t SEG_OPA_DIM = LV_OPA_20;
static const lv_opa_t SEG_OPA_ON = LV_OPA_COVER;

/* ---------------- "Real aircraft ASI" needle dynamics ----------------
   Same model as screen1: sensor LPF + 2nd-order damped response, with
   stiction + up/down feel differences + wn depends on speed.
*/
static float s_raw_ema = ASI_MIN;  // filtered sensor target (km/h)
static float s_x = ASI_MIN;        // displayed speed state (km/h)
static float s_v = 0.0f;           // displayed needle rate (km/h/s)
static uint32_t s_last_ms = 0;

/* Sensor filtering */
static constexpr float SENSOR_TAU_SEC = 0.18f; // 0.12..0.35

/* Mechanical model tuning */
static constexpr float ZETA_UP = 0.70f;   // accelerating damping
static constexpr float ZETA_DOWN = 0.82f; // decelerating damping

static constexpr float WN_LOW = 4.5f;  // rad/s at low speed
static constexpr float WN_HIGH = 9.0f; // rad/s at high speed

static constexpr float DECEL_LAG = 1.10f;         // >1 slows decel response
static constexpr float STICTION_BAND = 0.35f;     // km/h
static constexpr float MAX_RATE_KMH_PER_SEC = 420.0f;
static constexpr float MAX_ACCEL_KMH_PER_SEC2 = 2200.0f;

/* ---------- helpers ---------- */

static inline void make_noninteractive(lv_obj_t* o)
{
    if (!o) return;
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
}

static inline void make_screen_static(lv_obj_t* o)
{
    if (!o) return;
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

static inline float clampf(float x, float lo, float hi)
{
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

static inline float lerpf(float a, float b, float t)
{
    return a + (b - a) * t;
}

/* One segment per flap range => 0..count-1 */
static inline int32_t max_drawable_segment(void)
{
    if (s_seg_count < 1) return -1;
    return (int32_t)s_seg_count - 1;
}

static void set_target_segment(int32_t tgt)
{
    const int32_t max_seg = max_drawable_segment();
    if (max_seg < 0) return;

    /* Invalid => no highlight (dim previous only) */
    if (tgt < 0 || tgt > max_seg)
    {
        if (s_last_highlight_idx >= 0 && s_last_highlight_idx <= max_seg)
        {
            lv_obj_t* prev = s_seg_arcs[s_last_highlight_idx];
            if (prev) lv_obj_set_style_arc_opa(prev, SEG_OPA_DIM, LV_PART_INDICATOR);
        }
        s_last_highlight_idx = -1;
        return;
    }

    /* No change => do nothing */
    if (tgt == s_last_highlight_idx) return;

    /* First valid set: dim all once so only one becomes bright */
    if (s_last_highlight_idx == -9999)
    {
        for (int32_t i = 0; i <= max_seg && i < 31; i++)
        {
            if (s_seg_arcs[i]) lv_obj_set_style_arc_opa(s_seg_arcs[i], SEG_OPA_DIM, LV_PART_INDICATOR);
            if (i % 4 == 0) esp_task_wdt_reset();
        }
    }
    else if (s_last_highlight_idx >= 0 && s_last_highlight_idx <= max_seg)
    {
        /* Dim previous */
        lv_obj_t* prev = s_seg_arcs[s_last_highlight_idx];
        if (prev) lv_obj_set_style_arc_opa(prev, SEG_OPA_DIM, LV_PART_INDICATOR);
    }

    /* Bright new */
    lv_obj_t* cur = s_seg_arcs[tgt];
    if (cur) lv_obj_set_style_arc_opa(cur, SEG_OPA_ON, LV_PART_INDICATOR);

    s_last_highlight_idx = tgt;
}

/* Try both LVGL transform APIs (v8/v9 variants) */
static inline void set_label_rotation_01deg(lv_obj_t* obj, int32_t angle01deg)
{
#if defined(LVGL_VERSION_MAJOR) && (LVGL_VERSION_MAJOR >= 9)
    lv_obj_set_style_transform_rotation(obj, angle01deg, 0);
#else
    lv_obj_set_style_transform_angle(obj, angle01deg, 0);
#endif
}

/* Forward decl */
static void draw_variable_scale(lv_obj_t* parent,
                                const std::vector<flaputils::FlapSpeedRange>& params,
                                uint32_t count,
                                const float* w,
                                float w_sum,
                                int32_t rot_deg,
                                int32_t span_deg,
                                int32_t gap_deg);

/* Draw ticks at segment boundaries and labels at segment centers */
static void draw_variable_scale(lv_obj_t* parent,
                                const std::vector<flaputils::FlapSpeedRange>& params,
                                uint32_t count,
                                const float* w,
                                float w_sum,
                                int32_t rot_deg,
                                int32_t span_deg,
                                int32_t gap_deg)
{
    if (!parent) return;

    const int32_t W = lv_obj_get_width(parent);
    const int32_t H = lv_obj_get_height(parent);
    const int32_t cx = W / 2;
    const int32_t cy = H / 2;

    /* Geometry */
    const int32_t tick_outer_r = (W / 2) - 4;
    const int32_t tick_inner_r = tick_outer_r - 14;
    const int32_t label_r = tick_inner_r - 28;

    const int32_t tick_width = 2;

    const int32_t usable_span = span_deg - (int32_t)count * gap_deg;
    const float usable_span_f = (usable_span > 0) ? (float)usable_span : (float)span_deg;

    int32_t boundary[33] = {0};

    float acc = 0.0f;
    for (uint32_t i = 0; i <= count && i < 33; ++i)
    {
        float a_f = (w_sum > 0.0f) ? ((acc / w_sum) * usable_span_f) : 0.0f;
        boundary[i] = fast_roundf(a_f) + (int32_t)i * gap_deg;

        if (i < count) acc += w[i];
    }

    /* boundary ticks */
    for (uint32_t i = 0; i <= count && i < 33; ++i)
    {
        const float boundary_deg = (float)rot_deg + (float)boundary[i];
        const float r = deg2rad(boundary_deg);

        const int32_t x0 = cx + fast_roundf((float)tick_inner_r * cosf(r));
        const int32_t y0 = cy + fast_roundf((float)tick_inner_r * sinf(r));
        const int32_t x1 = cx + fast_roundf((float)tick_outer_r * cosf(r));
        const int32_t y1 = cy + fast_roundf((float)tick_outer_r * sinf(r));

        s_tick_pts[i][0].x = x0;
        s_tick_pts[i][0].y = y0;
        s_tick_pts[i][1].x = x1;
        s_tick_pts[i][1].y = y1;

        lv_obj_t* line = s_tick_lines[i];
        if (!line)
        {
            line = lv_line_create(parent);
            s_tick_lines[i] = line;
            make_noninteractive(line);
            lv_obj_set_style_line_width(line, tick_width, 0);
            lv_obj_set_style_line_color(line, lv_color_white(), 0);
        }

        lv_line_set_points(line, s_tick_pts[i], 2);
        lv_obj_remove_flag(line, LV_OBJ_FLAG_HIDDEN);
        
        // Feed watchdog during potentially long loop
        if (i % 4 == 0) {
            esp_task_wdt_reset();
        }
    }

    for (uint32_t i = count + 1; i < 33; ++i)
    {
        if (s_tick_lines[i]) lv_obj_add_flag(s_tick_lines[i], LV_OBJ_FLAG_HIDDEN);
    }

    /* labels at segment centers */
    for (uint32_t i = 0; i < count && i < 32; ++i)
    {
        const int32_t mid = (boundary[i] + boundary[i + 1]) / 2;
        const float label_deg = (float)rot_deg + (float)mid;
        const float r = deg2rad(label_deg);

        const int32_t lx = cx + fast_roundf((float)label_r * cosf(r));
        const int32_t ly = cy + fast_roundf((float)label_r * sinf(r));

        lv_obj_t* lab = s_labels[i];
        if (!lab)
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

        const lv_coord_t lw = 40;
        const lv_coord_t lh = 24;
        lv_obj_set_pos(lab, (lv_coord_t)(lx - lw / 2), (lv_coord_t)(ly - lh / 2));

        float tang_deg = label_deg + 90.0f;
        while (tang_deg < 0.0f) tang_deg += 360.0f;
        while (tang_deg >= 360.0f) tang_deg -= 360.0f;
        if (tang_deg > 90.0f && tang_deg < 270.0f) tang_deg += 180.0f;
        while (tang_deg >= 360.0f) tang_deg -= 360.0f;

        lv_obj_set_style_transform_pivot_x(lab, lw / 2, 0);
        lv_obj_set_style_transform_pivot_y(lab, lh / 2, 0);
        set_label_rotation_01deg(lab, (int32_t)(tang_deg * 10.0f));

        // Feed watchdog
        if (i % 4 == 0) {
            esp_task_wdt_reset();
        }
    }

    for (uint32_t i = count; i < 32; ++i)
    {
        if (s_labels[i]) lv_obj_add_flag(s_labels[i], LV_OBJ_FLAG_HIDDEN);
    }
}

/**
 * Custom needle update that supports an inner radius (gap from center)
 * (Your screen2 version uses fixed geometry constants; keep as-is.)
 */
static void ui_set_line_needle_value(lv_obj_t* /*scale_obj*/, lv_obj_t* needle_line,
                                     const int32_t inner_length, const int32_t outer_length, int32_t value)
{
    lv_obj_align(needle_line, LV_ALIGN_TOP_LEFT, 0, 0);

    int32_t rotation = 130;
    int32_t angle_range = 280;
    int32_t min = 40;
    int32_t max = 280;
    int32_t width = 466;
    int32_t height = 466;

    int32_t angle = 0;
    if (value > min)
    {
        if (value > max) angle = angle_range;
        else angle = static_cast<int32_t>((int64_t)angle_range * (value - min) / (max - min));
    }

    int32_t total_angle = rotation + angle;

    static lv_point_precise_t points[2];
    points[0].x = (width / 2) + ((inner_length * lv_trigo_cos(total_angle)) >> LV_TRIGO_SHIFT);
    points[0].y = (height / 2) + ((inner_length * lv_trigo_sin(total_angle)) >> LV_TRIGO_SHIFT);
    points[1].x = (width / 2) + ((outer_length * lv_trigo_cos(total_angle)) >> LV_TRIGO_SHIFT);
    points[1].y = (height / 2) + ((outer_length * lv_trigo_sin(total_angle)) >> LV_TRIGO_SHIFT);

    lv_line_set_points(needle_line, points, 2);
}

/* --------- ASI mechanics step + draw --------- */

static void asi_step_mechanics(float target_kmh)
{
    uint32_t now = lv_tick_get();
    float dt = 0.02f; // expected 50 Hz
    if (s_last_ms != 0)
    {
        dt = (now - s_last_ms) / 1000.0f;
        if (dt < 0.001f) dt = 0.001f;
        if (dt > 0.050f) dt = 0.050f;
    }
    s_last_ms = now;

    target_kmh = clampf(target_kmh, ASI_MIN, ASI_MAX);

    const float err0 = target_kmh - s_x;
    if (fabsf(err0) < STICTION_BAND && fabsf(s_v) < 2.0f)
    {
        s_x = target_kmh;
        s_v = 0.0f;
        return;
    }

    const float t = clampf((s_x - ASI_MIN) / (ASI_MAX - ASI_MIN), 0.0f, 1.0f);
    float wn = lerpf(WN_LOW, WN_HIGH, t);

    const bool accelerating = (target_kmh > s_x);
    float zeta = accelerating ? ZETA_UP : ZETA_DOWN;
    float lag = accelerating ? 1.0f : DECEL_LAG;
    wn /= lag;

    float a = (wn * wn) * (target_kmh - s_x) - (2.0f * zeta * wn) * s_v;
    a = clampf(a, -MAX_ACCEL_KMH_PER_SEC2, MAX_ACCEL_KMH_PER_SEC2);

    s_v += a * dt;
    s_v = clampf(s_v, -MAX_RATE_KMH_PER_SEC, MAX_RATE_KMH_PER_SEC);

    s_x += s_v * dt;
    s_x = clampf(s_x, ASI_MIN, ASI_MAX);

    const float err1 = target_kmh - s_x;
    if ((err0 > 0 && err1 < 0) || (err0 < 0 && err1 > 0))
    {
        s_v *= 0.55f;
    }
}

static void ui_update_asi(float raw_kmh)
{
    if (std::isnan(raw_kmh) || std::isinf(raw_kmh)) raw_kmh = ASI_MIN;
    raw_kmh = clampf(raw_kmh, ASI_MIN, ASI_MAX);

    static uint32_t last_ema_ms = 0;
    uint32_t now = lv_tick_get();
    float dt = 0.02f;
    if (last_ema_ms != 0)
    {
        dt = (now - last_ema_ms) / 1000.0f;
        if (dt < 0.001f) dt = 0.001f;
        if (dt > 0.100f) dt = 0.100f;
    }
    last_ema_ms = now;

    const float alpha = dt / (SENSOR_TAU_SEC + dt);
    s_raw_ema += alpha * (raw_kmh - s_raw_ema);

    asi_step_mechanics(s_raw_ema);

    const int32_t vi = (int32_t)lroundf(s_x);
    ui_set_line_needle_value(s_scale, s_needle, NEEDLE_INNER_RADIUS, NEEDLE_OUTER_RADIUS, vi);
}

/* ---------- deferred build ---------- */

static void ui_create_screen2_deferred(void)
{
    double weight = get_weight_kg();
    if (s_initialized && std::abs(weight - s_last_weight) < 0.5) return;

    /* Build labels + segments */
    auto params = flaputils::get_flap_speed_ranges(weight);
    if (params.empty()) return;

    uint32_t count = (uint32_t)params.size();
    if (count > 31) count = 31;

    s_seg_count = count;
    s_last_highlight_idx = -9999;

    const int32_t rot = 135;
    const int32_t span = 270;

    const int32_t ring_w = 20;
    const int32_t ring_size = 430;

    /* Compute weights */
    float w_sum = 0.0f;
    float w[32] = {0};

    for (uint32_t i = 0; i < count; ++i)
    {
        float lo = params[i].lower_speed;
        float hi = params[i].upper_speed;

        float wi = 0.0f;
        if (lo >= 0.0f && hi >= 0.0f && hi > lo) wi = hi - lo;
        else wi = 0.0f;

        w[i] = wi;
        w_sum += wi;
    }

    /* Fallback: if everything is NA/zero, distribute equally */
    if (w_sum <= 0.0f)
    {
        for (uint32_t i = 0; i < count; ++i) w[i] = 1.0f;
        w_sum = (float)count;
    }

    const int32_t gap_deg = 2;
    const int32_t usable_span = span - (int32_t)count * gap_deg;
    const float usable_span_f = (usable_span > 0) ? (float)usable_span : (float)span;

    float acc = 0.0f;

    for (uint32_t i = 0; i < 32; ++i)
    {
        if (i < count)
        {
            lv_obj_t* arc = s_seg_arcs[i];
            if (!arc)
            {
                arc = lv_arc_create(s_arc_container);
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
                lv_obj_set_style_arc_opa(arc, SEG_OPA_DIM, LV_PART_INDICATOR);
                lv_obj_move_background(arc);
            }

            float a0f = (acc / w_sum) * usable_span_f;
            acc += w[i];
            float a1f = (acc / w_sum) * usable_span_f;

            int32_t a0 = fast_roundf(a0f) + (int32_t)i * gap_deg;
            int32_t a1 = fast_roundf(a1f) + (int32_t)i * gap_deg;

            if (a1 <= a0) a1 = a0 + 1;

            lv_arc_set_angles(arc, (int16_t)a0, (int16_t)a1);
            lv_obj_remove_flag(arc, LV_OBJ_FLAG_HIDDEN);
            
            // Feed watchdog during creation of many objects
            if (i % 4 == 0) {
                esp_task_wdt_reset();
            }
        }
        else
        {
            if (s_seg_arcs[i]) lv_obj_add_flag(s_seg_arcs[i], LV_OBJ_FLAG_HIDDEN);
        }
    }

    draw_variable_scale(s_arc_container, params, count, w, w_sum, rot, span, gap_deg);

    s_initialized = true;
    s_last_weight = weight;

    /* Force target highlight update on next tick */
    s_last_target_idx = -9999;
}

/* ---------- timer ---------- */

static void ui_update_timer_cb(lv_timer_t* /*t*/)
{
    // Feeding the watchdog here is safe as this is called from the LVGL task context
    esp_task_wdt_reset();

    if (lv_screen_active() != s_screen) return;

    /* Do heavy work (weight-dependent rebuild) slower */
    static uint8_t slow_div = 0;
    slow_div++;
    if (slow_div >= 10) // 10 * 100ms = 1s
    {
        slow_div = 0;
        double current_weight = get_weight_kg();
        if (!s_initialized || std::abs(current_weight - s_last_weight) >= 0.5)
        {
            ui_create_screen2_deferred();
        }
    }

    /* Update flap label/triangles at moderate rate (same as timer: 100ms) */
    static uint8_t mid_div = 0;
    mid_div++;
    if (mid_div >= 2) // 2 * 100ms = 200ms
    {
        mid_div = 0;

        flaputils::FlapSymbolResult actual = get_flap_actual();
        flaputils::FlapSymbolResult target = get_flap_target();

        if (actual.index != s_last_actual_idx)
        {
            if (s_flap_label)
            {
                const char* sym = flaputils::get_flap_symbol_name(actual.index);
                lv_label_set_text(s_flap_label, sym ? sym : "-");
            }
            s_last_actual_idx = actual.index;
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

        if (target.index != s_last_target_idx)
        {
            set_target_segment(target.index);
            s_last_target_idx = target.index;
        }
    }

    /* Needle: smooth at full rate (100ms) - only update if visible */
    if (s_scale && s_needle && lv_screen_active() == s_screen)
    {
        ui_update_asi(get_ias_kmh());
    }
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
    s_arc_container = lv_obj_create(s_screen);
    make_noninteractive(s_arc_container);
    lv_obj_set_size(s_arc_container, 466, 466);
    lv_obj_center(s_arc_container);
    lv_obj_set_style_bg_opa(s_arc_container, LV_OPA_0, 0);
    lv_obj_set_style_border_width(s_arc_container, 0, 0);
    lv_obj_set_style_pad_all(s_arc_container, 0, 0);

    // Round inner scale 40..280 km/h (needle only)
    s_scale = lv_scale_create(s_screen);
    lv_obj_set_size(s_scale, 466, 466);
    lv_obj_center(s_scale);

    lv_scale_set_mode(s_scale, LV_SCALE_MODE_ROUND_INNER);
    lv_scale_set_range(s_scale, (int32_t)ASI_MIN, (int32_t)ASI_MAX);
    lv_scale_set_total_tick_count(s_scale, 11); // Hide ticks
    lv_scale_set_major_tick_every(s_scale, 5);
    lv_obj_set_style_line_width(s_scale, 0, LV_PART_ITEMS);
    lv_obj_set_style_line_width(s_scale, 0, LV_PART_INDICATOR);
    lv_scale_set_angle_range(s_scale, 280);
    lv_scale_set_rotation(s_scale, 130);
    lv_scale_set_label_show(s_scale, false);

    lv_obj_set_style_text_color(s_scale, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_scale, &lv_font_montserrat_20, 0);

    // Needle
    s_needle = lv_line_create(s_scale);
    lv_obj_set_style_line_width(s_needle, 12, 0);
    lv_obj_set_style_line_color(s_needle, lv_color_white(), 0);
    lv_obj_set_style_line_rounded(s_needle, true, 0);

    // Initial position (min)
    ui_set_line_needle_value(s_scale, s_needle, NEEDLE_INNER_RADIUS, NEEDLE_OUTER_RADIUS, (int32_t)ASI_MIN);

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

    /* Triangle below label */
    LV_DRAW_BUF_DEFINE_STATIC(draw_buf_down, 70, 70, LV_COLOR_FORMAT_ARGB8888);
    LV_DRAW_BUF_INIT_STATIC(draw_buf_down);
    s_triangle_down_canvas = lv_canvas_create(s_screen);
    make_noninteractive(s_triangle_down_canvas);
    lv_canvas_set_draw_buf(s_triangle_down_canvas, &draw_buf_down);
    lv_canvas_fill_bg(s_triangle_down_canvas, lv_color_black(), LV_OPA_0);

    lv_canvas_init_layer(s_triangle_down_canvas, &layer);

    tri_dsc.p[0].x = 35;
    tri_dsc.p[0].y = 65;
    tri_dsc.p[1].x = 0;
    tri_dsc.p[1].y = 5;
    tri_dsc.p[2].x = 70;
    tri_dsc.p[2].y = 5;

    lv_draw_triangle(&layer, &tri_dsc);
    lv_canvas_finish_layer(s_triangle_down_canvas, &layer);
    lv_obj_align_to(s_triangle_down_canvas, s_flap_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 40);

    lv_obj_add_flag(s_triangle_up_canvas, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_triangle_down_canvas, LV_OBJ_FLAG_HIDDEN);
}

void screen2_create()
{
    ui_create_screen2();
    ui_create_screen2_deferred();

    // Init needle dynamics to current IAS to avoid a jump
    float v = get_ias_kmh();
    if (std::isnan(v) || std::isinf(v)) v = ASI_MIN;
    v = clampf(v, ASI_MIN, ASI_MAX);

    s_raw_ema = v;
    s_x = v;
    s_v = 0.0f;
    s_last_ms = 0;

    // 10 Hz tick (100ms) for smooth needle; other UI updates are internally divided down
    lv_timer_create(ui_update_timer_cb, 100, nullptr);
}

lv_obj_t* screen2_get()
{
    return s_screen;
}

#else
void screen2_create() {}
lv_obj_t* screen2_get() { return nullptr; }
#endif