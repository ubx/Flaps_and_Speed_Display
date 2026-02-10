#ifndef FLAPUTILS_HPP
#define FLAPUTILS_HPP

#include <cstddef>

namespace flaputils {

// Returns the empty mass of the aircraft in kg (taken from flapDescriptor.json)
double get_empty_mass();

// Returns the flap symbol for a given raw position and the index in the table.
// If no match is found within tolerance, returns {nullptr, -1}.
struct FlapSymbolResult {
    const char* symbol; // nullptr if not found
    int index;          // -1 if not found
};

FlapSymbolResult get_flap_symbol(int position);

// Returns the optimal flap symbol (e.g. "L", "+2", "0", "S1") for a given
// total weight (kg) and indicated airspeed (km/h).
// Returns nullptr if no matching range is found or data is unavailable.
const char* get_optimal_flap(double gewicht_kg, double geschwindigkeit_kmh);

} // namespace flaputils

#endif // FLAPUTILS_HPP
