#pragma once

#include "flight_data.hpp"
#include "flaputils.hpp"
#include <cstdio>

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
