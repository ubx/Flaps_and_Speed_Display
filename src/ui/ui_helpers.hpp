#pragma once

#include "flight_data.hpp"
#include "flaputils.hpp"
#include "lvgl.h"
#include <cstdio>

struct StaleOverlayState {
    lv_obj_t* cross_a = nullptr;
    lv_obj_t* cross_b = nullptr;
    bool visible = false;
};

inline void ui_set_stale_overlay(lv_obj_t* parent, StaleOverlayState& state, bool show)
{
    if (show == state.visible) return;
    state.visible = show;

    if (show)
    {
        if (!state.cross_a)
        {
            static lv_point_precise_t cross_a_pts[2] = {{60, 60}, {406, 406}};
            state.cross_a = lv_line_create(parent);
            lv_line_set_points(state.cross_a, cross_a_pts, 2);
            lv_obj_set_style_line_width(state.cross_a, 10, 0);
            lv_obj_set_style_line_color(state.cross_a, lv_palette_main(LV_PALETTE_RED), 0);
            lv_obj_set_style_line_rounded(state.cross_a, true, 0);
        }
        lv_obj_remove_flag(state.cross_a, LV_OBJ_FLAG_HIDDEN);
        if (!state.cross_b)
        {
            static lv_point_precise_t cross_b_pts[2] = {{406, 60}, {60, 406}};
            state.cross_b = lv_line_create(parent);
            lv_line_set_points(state.cross_b, cross_b_pts, 2);
            lv_obj_set_style_line_width(state.cross_b, 10, 0);
            lv_obj_set_style_line_color(state.cross_b, lv_palette_main(LV_PALETTE_RED), 0);
            lv_obj_set_style_line_rounded(state.cross_b, true, 0);
        }
        lv_obj_remove_flag(state.cross_b, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    if (state.cross_a) lv_obj_add_flag(state.cross_a, LV_OBJ_FLAG_HIDDEN);
    if (state.cross_b) lv_obj_add_flag(state.cross_b, LV_OBJ_FLAG_HIDDEN);
}

inline float get_ias_kmh(const FlightData& state)
{
    std::lock_guard<std::mutex> lock(state.mtx);
    return state.ias * 3.6f;
}

inline float get_weight_kg(const FlightData& state)
{
    std::lock_guard<std::mutex> lock(state.mtx);
    return state.dry_and_ballast_mass / 10.0f + 84.0f;
}

inline float get_alt_m(const FlightData& state)
{
    std::lock_guard<std::mutex> lock(state.mtx);
    return state.alt;
}

inline float get_heading(const FlightData& state)
{
    std::lock_guard<std::mutex> lock(state.mtx);
    return state.heading;
}

inline float get_wind_speed_kmh(const FlightData& state)
{
    std::lock_guard<std::mutex> lock(state.mtx);
    return state.wind_speed * 3.6f;
}

inline float get_wind_direction(const FlightData& state)
{
    std::lock_guard<std::mutex> lock(state.mtx);
    return state.wind_direction;
}

inline float get_gps_ground_speed_kmh(const FlightData& state)
{
    std::lock_guard<std::mutex> lock(state.mtx);
    return state.gps_ground_speed * 3.6f;
}

inline float get_gps_true_track(const FlightData& state)
{
    std::lock_guard<std::mutex> lock(state.mtx);
    return state.gps_true_track;
}

inline flaputils::FlapSymbolResult get_flap_actual(const FlightData& state)
{
    std::lock_guard<std::mutex> lock(state.mtx);
    return flaputils::get_flap_symbol(state.flap);
}

inline flaputils::FlapSymbolResult get_flap_target(const FlightData& state)
{
    float weight = get_weight_kg(state);
    std::lock_guard<std::mutex> lock(state.mtx);
    return flaputils::get_optimal_flap(weight, state.ias * 3.6f);
}

inline void print_flight_data(const FlightData& state)
{
    std::lock_guard lock(state.mtx);
    printf(
        "FlightData: IAS=%.2f, TAS=%.2f, ALT=%.2f, Vario=%.2f, Flap=%d, Lat=%.7f, Lon=%.7f, GPS Ground Speed=%.2f, GPS True Track=%.2f, Dry + Ballast Mass=%u, ENL=%u, Wind Speed=%.2f, Wind Dir=%.2f, Heading=%.2f\n",
        state.ias * 3.6, state.tas * 3.6, state.alt, state.vario, state.flap, state.lat, state.lon, state.gps_ground_speed, state.gps_true_track, state.dry_and_ballast_mass / 10, state.enl, state.wind_speed, state.wind_direction, state.heading);

    const auto [index] = flaputils::get_optimal_flap(
        state.dry_and_ballast_mass / 10.0f + 84.0f, state.ias * 3.6f);
    const flaputils::FlapSymbolResult actual = flaputils::get_flap_symbol(state.flap);
    const char* opt_sym = flaputils::get_range_symbol_name(index);
    const char* act_sym = flaputils::get_flap_symbol_name(actual.index);
    printf("Flaps: Optimal=%s, Actual=%s\n",
           opt_sym ? opt_sym : "N/A",
           act_sym ? act_sym : "N/A");
}
