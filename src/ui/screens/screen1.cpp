#include <cmath>
#include "lvgl.h"
#include "../ui.h"
#include "../ui_helpers.hpp"
#include "flaputils.hpp"

// UI objects
extern const lv_font_t mono_digits_120;
static lv_obj_t* s_screen = nullptr;
static lv_obj_t* s_scale = nullptr;
static lv_obj_t* s_needle = nullptr;
static lv_obj_t* s_label = nullptr;
static StaleOverlayState s_stale_overlay;

// Needle dimensions
static constexpr int32_t NEEDLE_INNER_RADIUS = 120;
static constexpr int32_t NEEDLE_OUTER_RADIUS = 200;
static constexpr int32_t SCALE_LABEL_GAP = 26;
static constexpr int32_t ASI_ARC_WIDTH = 15;

// Scale range
static constexpr float ASI_MIN = 40.0f;
static constexpr float ASI_MAX = 280.0f;

// ASI Arc colors
#define ASI_COLOR_WHITE  lv_color_white()
#define ASI_COLOR_GREEN  lv_palette_main(LV_PALETTE_GREEN)
#define ASI_COLOR_YELLOW lv_palette_main(LV_PALETTE_YELLOW)
#define ASI_COLOR_RED    lv_palette_main(LV_PALETTE_RED)

// ---------- "Real aircraft ASI" needle dynamics ----------
//
// This implements a lightly-damped 2nd order system:
//   x' = v
//   v' = ωn^2 (target - x) - 2ζωn v
//
// plus:
// - a small deadband ("stiction") to stop micro-wiggle
// - different response when speeding up vs slowing down (hysteresis-ish feel)
// - ωn depends on speed (needle feels heavier at low speed)
// - optional overshoot via ζ < 1 (keep it modest)
//
// Tune the constants below.

static float s_raw_ema = ASI_MIN;      // filtered sensor target (km/h)
static float s_x = ASI_MIN;            // displayed speed state (km/h)
static float s_v = 0.0f;               // displayed speed rate state (km/h per sec)
static uint32_t s_last_ms = 0;

// Sensor filtering (removes jitter before feeding the “mechanical” model)
static constexpr float SENSOR_TAU_SEC = 0.18f; // 0.12..0.35

// Mechanical model tuning
static constexpr float ZETA_UP = 0.70f;   // damping ratio accelerating (0.6..0.9)
static constexpr float ZETA_DOWN = 0.82f; // damping ratio decelerating (0.7..1.0)

// Natural frequency vs speed (rad/s)
// Lower at low IAS feels “heavy”; higher at high IAS feels more responsive.
static constexpr float WN_LOW = 4.5f;     // rad/s at low speed
static constexpr float WN_HIGH = 9.0f;    // rad/s at high speed

// Optional extra “lag” on deceleration (gives more realistic slow fall)
// (1.0 = none; >1 slows decel response)
static constexpr float DECEL_LAG = 1.10f;

// Deadband (“stiction”) in km/h to stop needle from buzzing when nearly steady
static constexpr float STICTION_BAND = 0.35f;

// Max needle acceleration / rate clamps (stability + realism)
static constexpr float MAX_RATE_KMH_PER_SEC = 420.0f;     // cap speed of needle motion
static constexpr float MAX_ACCEL_KMH_PER_SEC2 = 2200.0f;  // cap acceleration

static float clampf(float x, float lo, float hi)
{
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

static float lerpf(float a, float b, float t)
{
    return a + (b - a) * t;
}

/**
 * Custom needle update that supports an inner radius (gap from center)
 */
static void ui_set_line_needle_value(lv_obj_t* scale_obj, lv_obj_t* needle_line, const int32_t inner_length,
                                     const int32_t outer_length, int32_t value)
{
    lv_obj_align(needle_line, LV_ALIGN_TOP_LEFT, 0, 0);

    int32_t rotation = lv_scale_get_rotation(scale_obj);
    int32_t angle_range = lv_scale_get_angle_range(scale_obj);
    int32_t min = lv_scale_get_range_min_value(scale_obj);
    int32_t max = lv_scale_get_range_max_value(scale_obj);
    int32_t width = lv_obj_get_style_width(scale_obj, LV_PART_MAIN);
    int32_t height = lv_obj_get_style_height(scale_obj, LV_PART_MAIN);

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

static void asi_step_mechanics(float target_kmh)
{
    // dt
    uint32_t now = lv_tick_get();
    float dt = 0.02f; // expected 50 Hz
    if (s_last_ms != 0)
    {
        dt = (now - s_last_ms) / 1000.0f;
        if (dt < 0.001f) dt = 0.001f;
        if (dt > 0.050f) dt = 0.050f; // avoid huge dt bursts
    }
    s_last_ms = now;

    // Clamp target to scale
    target_kmh = clampf(target_kmh, ASI_MIN, ASI_MAX);

    // --- "Stiction" deadband: if very close and moving slowly, stop ---
    const float err0 = target_kmh - s_x;
    if (fabsf(err0) < STICTION_BAND && fabsf(s_v) < 2.0f)
    {
        s_x = target_kmh;
        s_v = 0.0f;
        return;
    }

    // --- ωn depends on speed (heavier at low speed) ---
    const float t = clampf((s_x - ASI_MIN) / (ASI_MAX - ASI_MIN), 0.0f, 1.0f);
    float wn = lerpf(WN_LOW, WN_HIGH, t);

    // Different damping up/down + slower decel feel
    const bool accelerating = (target_kmh > s_x);
    float zeta = accelerating ? ZETA_UP : ZETA_DOWN;
    float lag = accelerating ? 1.0f : DECEL_LAG;
    wn /= lag;

    // Second-order dynamics
    // a = ωn^2 * (target - x) - 2ζωn * v
    float a = (wn * wn) * (target_kmh - s_x) - (2.0f * zeta * wn) * s_v;

    // Clamp acceleration (helps stability and realism)
    a = clampf(a, -MAX_ACCEL_KMH_PER_SEC2, MAX_ACCEL_KMH_PER_SEC2);

    // Integrate (semi-implicit Euler)
    s_v += a * dt;
    s_v = clampf(s_v, -MAX_RATE_KMH_PER_SEC, MAX_RATE_KMH_PER_SEC);

    s_x += s_v * dt;
    s_x = clampf(s_x, ASI_MIN, ASI_MAX);

    // If we crossed the target, damp out quickly to avoid long ringing
    // (keeps it "instrument-like" rather than "spring toy")
    const float err1 = target_kmh - s_x;
    if ((err0 > 0 && err1 < 0) || (err0 < 0 && err1 > 0))
    {
        s_v *= 0.55f;
    }
}

static void ui_update_asi(float raw_kmh)
{
    // sanitize & clamp to scale range
    if (std::isnan(raw_kmh) || std::isinf(raw_kmh)) raw_kmh = ASI_MIN;
    raw_kmh = clampf(raw_kmh, ASI_MIN, ASI_MAX);

    // time step for EMA (use lv_tick too, but keep it simple/robust)
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

    // 1) sensor low-pass
    const float alpha = dt / (SENSOR_TAU_SEC + dt);
    s_raw_ema += alpha * (raw_kmh - s_raw_ema);

    // 2) “mechanical” model step
    asi_step_mechanics(s_raw_ema);

    // 3) draw needle
    const int32_t vi = (int32_t)lroundf(s_x);
    ui_set_line_needle_value(s_scale, s_needle, NEEDLE_INNER_RADIUS, NEEDLE_OUTER_RADIUS, vi);

    // Update needle color based on current displayed speed
    lv_color_t n_color = ASI_COLOR_WHITE;
    flaputils::SpeedLimits sl = flaputils::get_speed_limits();
    if(s_x >= sl.vne) n_color = ASI_COLOR_RED;
    else if(s_x >= sl.vno) n_color = ASI_COLOR_YELLOW;
    else if(s_x >= sl.vs1) n_color = ASI_COLOR_GREEN;
    else if(s_x >= sl.vso) n_color = ASI_COLOR_WHITE;
    else n_color = ASI_COLOR_RED; // Below Vso

    lv_obj_set_style_line_color(s_needle, n_color, 0);
}


static inline void make_noninteractive(lv_obj_t* o)
{
    if (!o) return;
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
}

static void ui_create_gauge()
{
    s_screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(s_screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);

    // Round inner scale 40..280 km/h
    s_scale = lv_scale_create(s_screen);
    lv_obj_set_size(s_scale, 466, 466);
    lv_obj_center(s_scale);

    lv_scale_set_mode(s_scale, LV_SCALE_MODE_ROUND_INNER);
    lv_scale_set_range(s_scale, (int32_t)ASI_MIN, (int32_t)ASI_MAX);
    lv_scale_set_total_tick_count(s_scale, 25); // total ticks (40, 50, 60, ..., 280)
    lv_scale_set_major_tick_every(s_scale, 2);  // major each 2nd tick (40, 60, 80, ...)
    lv_scale_set_angle_range(s_scale, 280);
    lv_scale_set_rotation(s_scale, 130);
    lv_scale_set_label_show(s_scale, true);

    // Style the ticks (white scale lines)
    lv_obj_set_style_line_color(s_scale, lv_color_white(), LV_PART_INDICATOR);
    lv_obj_set_style_line_width(s_scale, 6, LV_PART_INDICATOR);
    lv_obj_set_style_length(s_scale, 24, LV_PART_INDICATOR);

    // lv_obj_set_style_line_color(s_scale, lv_color_white(), LV_PART_ITEMS);
    // lv_obj_set_style_line_width(s_scale, 1, LV_PART_ITEMS);
    // lv_obj_set_style_length(s_scale, 8, LV_PART_ITEMS);

    lv_obj_set_style_pad_radial(s_scale, SCALE_LABEL_GAP, LV_PART_INDICATOR);

    // Arcs
    static lv_style_t style_white;
    lv_style_init(&style_white);
    lv_style_set_arc_color(&style_white, ASI_COLOR_WHITE);
    lv_style_set_arc_width(&style_white, ASI_ARC_WIDTH + 5);

    static lv_style_t style_green;
    lv_style_init(&style_green);
    lv_style_set_arc_color(&style_green, ASI_COLOR_GREEN);
    lv_style_set_arc_width(&style_green, ASI_ARC_WIDTH);

    static lv_style_t style_yellow;
    lv_style_init(&style_yellow);
    lv_style_set_arc_color(&style_yellow, ASI_COLOR_YELLOW);
    lv_style_set_arc_width(&style_yellow, ASI_ARC_WIDTH);

    lv_scale_section_t* sec;
    flaputils::SpeedLimits sl = flaputils::get_speed_limits();

    // White arc: Vso to Vfe
    sec = lv_scale_add_section(s_scale);
    lv_scale_set_section_range(s_scale, sec, (int32_t)sl.vso, (int32_t)sl.vfe);
    lv_scale_set_section_style_main(s_scale, sec, &style_white);

    // Green arc: Vs1 to Vno
    sec = lv_scale_add_section(s_scale);
    lv_scale_set_section_range(s_scale, sec, (int32_t)sl.vs1, (int32_t)sl.vno);
    lv_scale_set_section_style_main(s_scale, sec, &style_green);

    // Yellow arc: Vno to Vne
    sec = lv_scale_add_section(s_scale);
    lv_scale_set_section_range(s_scale, sec, (int32_t)sl.vno, (int32_t)sl.vne);
    lv_scale_set_section_style_main(s_scale, sec, &style_yellow);

    lv_obj_set_style_text_color(s_scale, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_scale, &lv_font_montserrat_28, 0);

    // Needle
    s_needle = lv_line_create(s_scale);
    lv_obj_set_style_line_width(s_needle, 11, 0);
    lv_obj_set_style_line_color(s_needle, ASI_COLOR_WHITE, 0);
    lv_obj_set_style_line_rounded(s_needle, true, 0);

    // Center value
    s_label = lv_label_create(s_screen);
    lv_obj_set_style_text_color(s_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_label, &mono_digits_120, 0);
    lv_label_set_text(s_label, "--");
    lv_obj_center(s_label);

    // Unit
    lv_obj_t* unit = lv_label_create(s_screen);
    lv_obj_set_style_text_color(unit, lv_color_white(), 0);
    lv_obj_set_style_text_font(unit, &lv_font_montserrat_16, 0);
    lv_label_set_text(unit, "km/h");
    lv_obj_align(unit, LV_ALIGN_CENTER, 0, 80);

    /* Title */
    lv_obj_t* title = lv_label_create(s_screen);
    make_noninteractive(title);
    lv_label_set_text(title, "IAS");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_align(title, LV_ALIGN_BOTTOM_MID, 0, -10);

    // Initial position (min of scale)
    ui_set_line_needle_value(s_scale, s_needle, NEEDLE_INNER_RADIUS, NEEDLE_OUTER_RADIUS, (int32_t)ASI_MIN);

    // Stale overlay starts removed and is created on demand.
    s_stale_overlay = {};
}

static void ui_update_timer_cb(lv_timer_t* /*t*/)
{
    if (lv_screen_active() != s_screen) return;

    const bool stale = is_stale();
    ui_set_stale_overlay(s_screen, s_stale_overlay, stale);

    const float v = get_ias_kmh();

    if (s_scale && s_needle)
    {
        ui_update_asi(v);
    }

    // Update label slower (100 ms) to reduce redraw cost/flicker
    static uint8_t div = 0;
    div++;
    if (div >= 5) // 5 * 20ms = 100ms
    {
        div = 0;
        if (s_label)
        {
            lv_label_set_text_fmt(s_label, "%d", (int)v);
        }
    }
}

void screen1_create()
{
    ui_create_gauge();

    // Initialize states from current value to avoid initial jump
    float v = get_ias_kmh();
    if (std::isnan(v) || std::isinf(v)) v = ASI_MIN;
    v = clampf(v, ASI_MIN, ASI_MAX);

    s_raw_ema = v;
    s_x = v;
    s_v = 0.0f;
    s_last_ms = 0;

    // Smooth "instrument-like" needle: 25 Hz
    lv_timer_create(ui_update_timer_cb, 40, nullptr);
}

lv_obj_t* screen1_get()
{
    return s_screen;
}
