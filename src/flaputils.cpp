#include "flaputils.hpp"

#include <vector>
#include <string>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#if __has_include(<cjson/cJSON.h>)
#include <cjson/cJSON.h>
#else
#include "cJSON.h"
#endif

namespace flaputils {

struct FlapEntry {
    int position;
    std::string symbol;
};

static int kTolerance = 6;
static std::vector<FlapEntry> kFlapTable;
static std::vector<int> kWeights;
static double kEmptyMassKg = 0.0;

struct Range { double vmin; double vmax; };
struct Bereich {
    std::string wk;
    std::vector<Range> ranges;
};
static std::vector<Bereich> kBereiche;

bool load_data(const char* filepath) {
    FILE* f = fopen(filepath, "rb");
    if (!f) {
        printf("flaputils: Failed to open %s\n", filepath);
        return false;
    }

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (len <= 0) {
        printf("flaputils: File %s is empty\n", filepath);
        fclose(f);
        return false;
    }

    char* buffer = (char*)malloc(len + 1);
    if (!buffer) {
        printf("flaputils: Failed to allocate %ld bytes\n", len + 1);
        fclose(f);
        return false;
    }
    size_t read_len = fread(buffer, 1, len, f);
    fclose(f);
    buffer[read_len] = '\0';

    cJSON* root = cJSON_Parse(buffer);
    if (!root) {
        printf("flaputils: Failed to parse JSON\n");
        free(buffer);
        return false;
    }

    // Clear existing data
    kFlapTable.clear();
    kWeights.clear();
    kBereiche.clear();

    // 1. flap2symbol
    cJSON* f2s = cJSON_GetObjectItem(root, "flap2symbol");
    if (f2s) {
        cJSON* tol = cJSON_GetObjectItem(f2s, "tolerance");
        if (tol) kTolerance = tol->valueint;

        cJSON* table = cJSON_GetObjectItem(f2s, "table");
        if (table && cJSON_IsArray(table)) {
            int sz = cJSON_GetArraySize(table);
            for (int i = 0; i < sz; i++) {
                cJSON* item = cJSON_GetArrayItem(table, i);
                if (cJSON_IsArray(item) && cJSON_GetArraySize(item) >= 2) {
                    int pos = cJSON_GetArrayItem(item, 0)->valueint;
                    cJSON* sym_item = cJSON_GetArrayItem(item, 1);
                    if (sym_item && sym_item->valuestring) {
                        kFlapTable.push_back({pos, sym_item->valuestring});
                    }
                }
            }
        }
    }

    // 2. speedpolar
    cJSON* sp = cJSON_GetObjectItem(root, "speedpolar");
    if (sp) {
        cJSON* em = cJSON_GetObjectItem(sp, "empty_mass_kg");
        if (em) kEmptyMassKg = em->valuedouble;

        cJSON* opt = cJSON_GetObjectItem(sp, "optimale_fluggeschwindigkeit_kmh");
        if (opt) {
            cJSON* w_arr = cJSON_GetObjectItem(opt, "gewicht_kg");
            if (w_arr && cJSON_IsArray(w_arr)) {
                int sz = cJSON_GetArraySize(w_arr);
                for (int i = 0; i < sz; i++) {
                    kWeights.push_back(cJSON_GetArrayItem(w_arr, i)->valueint);
                }
            }

            cJSON* b_arr = cJSON_GetObjectItem(opt, "bereiche");
            if (b_arr && cJSON_IsArray(b_arr)) {
                int b_sz = cJSON_GetArraySize(b_arr);
                for (int i = 0; i < b_sz; i++) {
                    cJSON* b_item = cJSON_GetArrayItem(b_arr, i);
                    Bereich b;
                    cJSON* wk = cJSON_GetObjectItem(b_item, "wk");
                    if (wk && wk->valuestring) b.wk = wk->valuestring;

                    cJSON* g_obj = cJSON_GetObjectItem(b_item, "geschwindigkeit");
                    if (g_obj) {
                        for (int w : kWeights) {
                            char w_str[16];
                            snprintf(w_str, sizeof(w_str), "%d", w);
                            cJSON* r_arr = cJSON_GetObjectItem(g_obj, w_str);
                            if (r_arr && cJSON_IsArray(r_arr) && cJSON_GetArraySize(r_arr) >= 2) {
                                b.ranges.push_back({cJSON_GetArrayItem(r_arr, 0)->valuedouble,
                                                    cJSON_GetArrayItem(r_arr, 1)->valuedouble});
                            } else {
                                b.ranges.push_back({-1.0, -1.0}); // NA
                            }
                        }
                    }
                    kBereiche.push_back(b);
                }
            }
        }
    }

    cJSON_Delete(root);
    free(buffer);
    return true;
}

double get_empty_mass() { return kEmptyMassKg; }

FlapSymbolResult get_flap_symbol(int position) {
    for (std::size_t i = 0; i < kFlapTable.size(); ++i) {
        const auto& e = kFlapTable[i];
        if (std::abs(position - e.position) <= kTolerance) {
            return {e.symbol.c_str(), static_cast<int>(i)};
        }
    }
    return {nullptr, -1};
}

static inline void weight_bracket(double w, int& i1, int& i2, double& factor) {
    if (kWeights.empty()) {
        i1 = i2 = 0; factor = 0.0; return;
    }
    if (w <= kWeights.front()) {
        i1 = i2 = 0; factor = 0.0; return;
    }
    if (w >= kWeights.back()) {
        i1 = i2 = static_cast<int>(kWeights.size() - 1); factor = 0.0; return;
    }
    for (std::size_t i = 0; i + 1 < kWeights.size(); ++i) {
        if (w >= kWeights[i] && w <= kWeights[i + 1]) {
            i1 = static_cast<int>(i);
            i2 = static_cast<int>(i + 1);
            factor = (w - kWeights[i1]) / (double)(kWeights[i2] - kWeights[i1]);
            return;
        }
    }
    i1 = i2 = 0; factor = 0.0;
}

static inline bool has_range(const Range& r) { return r.vmin >= 0.0 && r.vmax >= 0.0; }

FlapSymbolResult get_optimal_flap(double gewicht_kg, double geschwindigkeit_kmh) {
    if (kBereiche.empty() || kWeights.empty()) return {nullptr, -1};

    int i1 = 0, i2 = 0; double f = 0.0;
    weight_bracket(gewicht_kg, i1, i2, f);

    for (std::size_t idx = 0; idx < kBereiche.size(); ++idx) {
        const auto& b = kBereiche[idx];
        if (b.ranges.size() <= (std::size_t)std::max(i1, i2)) continue;
        const Range r1 = b.ranges[i1];
        const Range r2 = b.ranges[i2];
        if (has_range(r1) && has_range(r2)) {
            const double vmin = r1.vmin + f * (r2.vmin - r1.vmin);
            const double vmax = r1.vmax + f * (r2.vmax - r1.vmax);
            if (geschwindigkeit_kmh >= vmin && geschwindigkeit_kmh <= vmax) return {b.wk.c_str(), static_cast<int>(idx)};
        } else if (has_range(r1)) {
            if (geschwindigkeit_kmh >= r1.vmin && geschwindigkeit_kmh <= r1.vmax) return {b.wk.c_str(), static_cast<int>(idx)};
        } else if (has_range(r2)) {
            if (geschwindigkeit_kmh >= r2.vmin && geschwindigkeit_kmh <= r2.vmax) return {b.wk.c_str(), static_cast<int>(idx)};
        }
    }
    return {nullptr, -1};
}

} // namespace flaputils
