#ifndef FLAPUTILS_HPP
#define FLAPUTILS_HPP

#include <vector>

#include "../../../../../../home/andreas/.platformio/packages/toolchain-riscv32-esp/riscv32-esp-elf/include/c++/14.2.0/string"

namespace flaputils
{
    // Returns the empty mass of the aircraft in kg (taken from flapDescriptor.json)
    double get_empty_mass();

    // Loads the flap data from a JSON file. Returns true on success.
    bool load_data(const char* filepath);

    // Returns the flap symbol for a given raw position and the index in the table.
    // If no match is found within tolerance, returns {nullptr, -1}.
    struct FlapSymbolResult
    {
        const char* symbol; // nullptr if not found
        int index; // -1 if not found
    };



    FlapSymbolResult get_flap_symbol(int position);

    // Returns the optimal flap symbol (e.g. "L", "+2", "0", "S1") for a given
    // total weight (kg) and indicated airspeed (km/h).
    // Returns {nullptr, -1} if no matching range is found or data is unavailable.
    FlapSymbolResult get_optimal_flap(double gewicht_kg, double geschwindigkeit_kmh);

    // std::vector<FlapSymbolResult> get_flap_params();

    struct FlapSpeedRange
    {
        const char* symbol;
        int index; // -1 if not found
        float lower_speed;
        float upper_speed;
    };

    std::vector<FlapSpeedRange> get_flap_speed_ranges(double gewicht_kg);

} // namespace flaputils

#endif // FLAPUTILS_HPP
