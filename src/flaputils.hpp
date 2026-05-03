#pragma once

#include <vector>
#include <string>

namespace flaputils
{
    // Returns the empty mass of the aircraft in kg (taken from ventus3_defaut.json)
    float get_empty_mass();

    // Loads the flap data from a JSON file. Returns true on success.
    bool load_data(const char* filepath);

    // Returns the flap symbol for a given raw position and the index in the table.
    // If no match is found within tolerance, returns {nullptr, -1}.
    struct FlapSymbolResult
    {
        int index; // -1 if not found
    };



    FlapSymbolResult get_flap_symbol(int flapIdx);

    // Returns the optimal flap symbol (e.g. "L", "+2", "0", "S1") for a given
    // total weight (kg) and indicated airspeed (km/h).
    // Returns {nullptr, -1} if no matching range is found or data is unavailable.
    FlapSymbolResult get_optimal_flap(float gewicht_kg, float geschwindigkeit_kmh);

    // Returns the symbol for a given flap index.
    const char* get_flap_symbol_name(int index);

    // Returns the symbol for a given speed range index.
    const char* get_range_symbol_name(int index);

    // Returns the current loaded polar filename.
    const char* get_polar();

    // Persists the current polar path to NVS.
    bool save_polar_path(const char* filepath);

    // Loads the polar data from the persisted path in NVS. Returns true on success.
    bool load_persisted_data();

    // Returns the first available polar file path in SPIFFS.
    std::string find_first_polar_path();

    // std::vector<FlapSymbolResult> get_flap_params();

    struct FlapSpeedRange
    {
        int index; // -1 if not found
        float lower_speed;
        float upper_speed;
    };

    std::vector<FlapSpeedRange> get_flap_speed_ranges(float gewicht_kg);

} // namespace flaputils
