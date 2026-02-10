#include "flaputils.hpp"

#include <array>
#include <cmath>

namespace flaputils {

// --- Data embedded from data/flapDescriptor.json ---

struct FlapEntry { int position; const char* symbol; };

static constexpr int kTolerance = 6; // flap2symbol.tolerance
static constexpr std::array<FlapEntry, 8> kFlapTable{{
    {94,  "L"},
    {167, "+2"},
    {243, "+1"},
    {84,  "0"},
    {156, "-1"},
    {191, "-2"},
    {230, "S"},
    {250, "S1"}
}};

static constexpr std::array<int, 4> kWeights{{390, 430, 550, 600}}; // kg

// For each Bereich (range group), keep wk and speed ranges per weight index.
// Use {-1,-1} to indicate "no data" for that weight.
struct Range { double vmin; double vmax; };
struct Bereich { const char* wk; std::array<Range, kWeights.size()> ranges; };

static constexpr double kEmptyMassKg = 373.15; // speedpolar.empty_mass_kg

static constexpr Range NA{-1.0, -1.0};

static constexpr std::array<Bereich, 7> kBereiche{{
    // "L"
    {"L",  std::array<Range, kWeights.size()>{ Range{0, 76},   Range{0, 80},   Range{0, 90},   Range{0, 94} }},
    // "+2"
    {"+2", std::array<Range, kWeights.size()>{ Range{76, 80},  Range{80, 83},  Range{90, 94},  Range{94, 98} }},
    // "+1"
    {"+1", std::array<Range, kWeights.size()>{ Range{80, 90},  Range{83, 94},  Range{94, 106}, Range{98, 111} }},
    // "0"
    {"0",  std::array<Range, kWeights.size()>{ Range{90, 122}, Range{94, 128}, Range{106,145}, Range{111,151} }},
    // "-1"
    {"-1", std::array<Range, kWeights.size()>{ Range{122,150}, Range{128,158}, Range{145,179}, Range{151,187} }},
    // "-2"
    {"-2", std::array<Range, kWeights.size()>{ Range{150,169}, Range{158,178}, Range{179,201}, Range{187,210} }},
    // "S"
    {"S",  std::array<Range, kWeights.size()>{ Range{169,188}, Range{178,198}, Range{201,224}, Range{210,234} }},
    // "S1"
}};

static constexpr std::array<Bereich, 1> kBereiche_S1{{
    {"S1", std::array<Range, kWeights.size()>{ Range{188,280}, Range{198,280}, Range{224,280}, Range{234,280} }}
}};

// --- Public API implementations ---

double get_empty_mass() { return kEmptyMassKg; }

FlapSymbolResult get_flap_symbol(int position) {
    for (std::size_t i = 0; i < kFlapTable.size(); ++i) {
        const auto& e = kFlapTable[i];
        if (std::abs(position - e.position) <= kTolerance) {
            return {e.symbol, static_cast<int>(i)};
        }
    }
    return {nullptr, -1};
}

// Helper to find interpolation indices and factor for a given weight.
static inline void weight_bracket(double w, int& i1, int& i2, double& factor) {
    if (w <= kWeights.front()) { i1 = i2 = 0; factor = 0.0; return; }
    if (w >= kWeights.back())  { i1 = i2 = static_cast<int>(kWeights.size()-1); factor = 0.0; return; }
    // find interval
    for (std::size_t i = 0; i + 1 < kWeights.size(); ++i) {
        if (w >= kWeights[i] && w <= kWeights[i+1]) {
            i1 = static_cast<int>(i);
            i2 = static_cast<int>(i+1);
            const double w1 = static_cast<double>(kWeights[i1]);
            const double w2 = static_cast<double>(kWeights[i2]);
            factor = (w - w1) / (w2 - w1);
            return;
        }
    }
    // Fallback (should not reach here)
    i1 = i2 = 0; factor = 0.0;
}

static inline bool has_range(const Range& r) { return r.vmin >= 0.0 && r.vmax >= 0.0; }

const char* get_optimal_flap(double gewicht_kg, double geschwindigkeit_kmh) {
    int i1 = 0, i2 = 0; double f = 0.0;
    weight_bracket(gewicht_kg, i1, i2, f);

    auto in_interpolated = [&](const Bereich& b) -> bool {
        const Range r1 = b.ranges[static_cast<std::size_t>(i1)];
        const Range r2 = b.ranges[static_cast<std::size_t>(i2)];
        if (has_range(r1) && has_range(r2)) {
            const double vmin = r1.vmin + f * (r2.vmin - r1.vmin);
            const double vmax = r1.vmax + f * (r2.vmax - r1.vmax);
            return (geschwindigkeit_kmh >= vmin && geschwindigkeit_kmh <= vmax);
        } else if (has_range(r1)) {
            return (geschwindigkeit_kmh >= r1.vmin && geschwindigkeit_kmh <= r1.vmax);
        } else if (has_range(r2)) {
            return (geschwindigkeit_kmh >= r2.vmin && geschwindigkeit_kmh <= r2.vmax);
        }
        return false;
    };

    // Iterate through ranges in the same logical order as JSON
    for (const auto& b : kBereiche) {
        if (in_interpolated(b)) return b.wk;
    }
    for (const auto& b : kBereiche_S1) {
        if (in_interpolated(b)) return b.wk;
    }
    return nullptr;
}

} // namespace flaputils
