#pragma once

#include <mutex>
#include <string>
#include <cstdint>

#ifdef NATIVE_TEST_BUILD
#include <chrono>
#else
#include "esp_timer.h"
#endif

struct FlightData
{
    mutable std::mutex mtx;
    float ias = 0;
    float tas = 0;
    float alt = 0;
    float vario = 0;
    int flap = 0;
    double lat = 0;
    double lon = 0;
    float gps_ground_speed = 0;
    float gps_true_track = 0;
    uint16_t dry_and_ballast_mass = 0;
    uint16_t enl = 0;
    float wind_speed = 0;
    float wind_direction = 0;
    float heading = 0;
    uint64_t last_relevant_rx_ms = 0;

    static uint64_t monotonic_ms()
    {
#ifdef NATIVE_TEST_BUILD
        using namespace std::chrono;
        return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
#else
        return esp_timer_get_time() / 1000ULL;
#endif
    }

    void update_float(const std::string& key, float value)
    {
        std::lock_guard<std::mutex> lock(mtx);
        if (key == "ias")
        {
            ias = value;
            last_relevant_rx_ms = monotonic_ms();
        }
        else if (key == "tas") tas = value;
        else if (key == "alt") alt = value;
        else if (key == "vario") vario = value;
        else if (key == "gps_ground_speed")
        {
            gps_ground_speed = value;
            last_relevant_rx_ms = monotonic_ms();
        }
        else if (key == "gps_true_track") gps_true_track = value;
        else if (key == "wind_speed") wind_speed = value;
        else if (key == "wind_direction") wind_direction = value;
        else if (key == "heading") heading = value;
    }

    void update_double(const std::string& key, double value)
    {
        std::lock_guard lock(mtx);
        if (key == "lat") lat = value;
        else if (key == "lon") lon = value;
    }

    void update_int(const std::string& key, int value)
    {
        std::lock_guard lock(mtx);
        if (key == "flap")
        {
            flap = value;
            last_relevant_rx_ms = monotonic_ms();
        }
    }

    void update_uint16(const std::string& key, uint16_t value)
    {
        std::lock_guard lock(mtx);
        if (key == "dry_and_ballast_mass") dry_and_ballast_mass = value;
        else if (key == "enl") enl = value;
    }

    bool is_stale() const
    {
        std::lock_guard lock(mtx);
        const uint64_t now_ms = monotonic_ms();
        if (last_relevant_rx_ms == 0)
        {
            return now_ms >= 10000ULL; // STALE_TIMEOUT_MS
        }
        return (now_ms - last_relevant_rx_ms) >= 10000ULL;
    }
};
