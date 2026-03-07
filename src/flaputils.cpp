#include "flaputils.hpp"

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

namespace flaputils
{
    struct FlapEntry
    {
        int position;
        std::string symbol;
    };

    static int kTolerance = 6;
    static std::vector<FlapEntry> kFlapTable;
    static std::vector<int> kWeights;
    static float kEmptyMassKg = 0.0f;
    static std::string kLowSpeedWk;

    struct Range
    {
        float vmin;
        float vmax;
    };

    struct Bereich
    {
        std::string wk;
        std::vector<Range> ranges;
    };

    static std::vector<Bereich> kBereiche;
    static Range kLowSpeedRange{-1.0f, -1.0f};

    static int find_flap_index_by_symbol(const std::string& symbol)
    {
        for (std::size_t i = 0; i < kFlapTable.size(); ++i)
        {
            if (kFlapTable[i].symbol == symbol)
            {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    bool load_data(const char* filepath)
    {
        FILE* f = fopen(filepath, "rb");
        if (!f)
        {
            printf("flaputils: Failed to open %s\n", filepath);
            return false;
        }

        fseek(f, 0, SEEK_END);
        long len = ftell(f);
        fseek(f, 0, SEEK_SET);

        if (len <= 0)
        {
            printf("flaputils: File %s is empty\n", filepath);
            fclose(f);
            return false;
        }

        char* buffer = static_cast<char*>(malloc(len + 1));
        if (!buffer)
        {
            printf("flaputils: Failed to allocate %ld bytes\n", len + 1);
            fclose(f);
            return false;
        }
        size_t read_len = fread(buffer, 1, len, f);
        fclose(f);
        buffer[read_len] = '\0';

        cJSON* root = cJSON_Parse(buffer);
        if (!root)
        {
            printf("flaputils: Failed to parse JSON\n");
            free(buffer);
            return false;
        }

        // Clear existing data
        kFlapTable.clear();
        kWeights.clear();
        kBereiche.clear();
        kLowSpeedWk.clear();
        kLowSpeedRange = {-1.0f, -1.0f};

        // 1. flap2symbol
        if (const cJSON* f2s = cJSON_GetObjectItem(root, "flap2symbol"))
        {
            if (cJSON* tol = cJSON_GetObjectItem(f2s, "tolerance")) kTolerance = tol->valueint;

            cJSON* table = cJSON_GetObjectItem(f2s, "table");
            if (table && cJSON_IsArray(table))
            {
                int sz = cJSON_GetArraySize(table);
                for (int i = 0; i < sz; i++)
                {
                    cJSON* item = cJSON_GetArrayItem(table, i);
                    if (cJSON_IsArray(item) && cJSON_GetArraySize(item) >= 2)
                    {
                        const int pos = cJSON_GetArrayItem(item, 0)->valueint;
                        cJSON* sym_item = cJSON_GetArrayItem(item, 1);
                        if (sym_item && sym_item->valuestring)
                        {
                            kFlapTable.push_back({pos, sym_item->valuestring});
                        }
                    }
                }
            }
        }

        // 2. speedpolar
        if (const cJSON* sp = cJSON_GetObjectItem(root, "speedpolar"))
        {
            if (cJSON* em = cJSON_GetObjectItem(sp, "empty_mass_kg")) kEmptyMassKg = static_cast<float>(em->valuedouble);

            if (cJSON* opt = cJSON_GetObjectItem(sp, "optimale_fluggeschwindigkeit_kmh"))
            {
                cJSON* w_arr = cJSON_GetObjectItem(opt, "gewicht_kg");
                if (w_arr && cJSON_IsArray(w_arr))
                {
                    int sz = cJSON_GetArraySize(w_arr);
                    for (int i = 0; i < sz; i++)
                    {
                        kWeights.push_back(cJSON_GetArrayItem(w_arr, i)->valueint);
                    }
                }

                cJSON* b_arr = cJSON_GetObjectItem(opt, "bereiche");
                if (b_arr && cJSON_IsArray(b_arr))
                {
                    int b_sz = cJSON_GetArraySize(b_arr);
                    for (int i = 0; i < b_sz; i++)
                    {
                        cJSON* b_item = cJSON_GetArrayItem(b_arr, i);
                        Bereich b;
                        cJSON* wk = cJSON_GetObjectItem(b_item, "wk");
                        if (wk && wk->valuestring) b.wk = wk->valuestring;

                        if (cJSON* g_obj = cJSON_GetObjectItem(b_item, "geschwindigkeit"))
                        {
                            for (int w : kWeights)
                            {
                                char w_str[16];
                                snprintf(w_str, sizeof(w_str), "%d", w);
                                cJSON* r_arr = cJSON_GetObjectItem(g_obj, w_str);
                                if (r_arr && cJSON_IsArray(r_arr) && cJSON_GetArraySize(r_arr) >= 2)
                                {
                                    b.ranges.push_back({
                                        static_cast<float>(cJSON_GetArrayItem(r_arr, 0)->valuedouble),
                                        static_cast<float>(cJSON_GetArrayItem(r_arr, 1)->valuedouble)
                                    });
                                }
                                else
                                {
                                    b.ranges.push_back({-1.0f, -1.0f}); // NA
                                }
                            }
                        }
                        kBereiche.push_back(b);
                    }
                }
            }
        }

        // 3. lowspeed
        if (const cJSON* ls = cJSON_GetObjectItem(root, "lowspeed"))
        {
            if (const cJSON* wk = cJSON_GetObjectItem(ls, "wk"); wk && wk->valuestring)
            {
                kLowSpeedWk = wk->valuestring;
            }

            if (const cJSON* geschwindigkeit = cJSON_GetObjectItem(ls, "geschwindigkeit"))
            {
                if (cJSON* wildcard = cJSON_GetObjectItem(geschwindigkeit, "*");
                    wildcard && cJSON_IsArray(wildcard) && cJSON_GetArraySize(wildcard) >= 2)
                {
                    kLowSpeedRange = {
                        static_cast<float>(cJSON_GetArrayItem(wildcard, 0)->valuedouble),
                        static_cast<float>(cJSON_GetArrayItem(wildcard, 1)->valuedouble)
                    };
                }
            }
        }

        cJSON_Delete(root);
        free(buffer);
        return true;
    }

    float get_empty_mass() { return kEmptyMassKg; }

    FlapSymbolResult get_flap_symbol(int position)
    {
        for (std::size_t i = 0; i < kFlapTable.size(); ++i)
        {
            const auto& e = kFlapTable[i];
            if (std::abs(position - e.position) <= kTolerance)
            {
                return {static_cast<int>(i)};
            }
        }
        return {-1};
    }

    static inline void weight_bracket(float w, int& i1, int& i2, float& factor)
    {
        if (kWeights.empty())
        {
            i1 = i2 = 0;
            factor = 0.0f;
            return;
        }
        if (w <= kWeights.front())
        {
            i1 = i2 = 0;
            factor = 0.0f;
            return;
        }
        if (w >= kWeights.back())
        {
            i1 = i2 = static_cast<int>(kWeights.size() - 1);
            factor = 0.0f;
            return;
        }
        for (std::size_t i = 0; i + 1 < kWeights.size(); ++i)
        {
            if (w >= kWeights[i] && w <= kWeights[i + 1])
            {
                i1 = static_cast<int>(i);
                i2 = static_cast<int>(i + 1);
                factor = (w - static_cast<float>(kWeights[i1])) /
                         static_cast<float>(kWeights[i2] - kWeights[i1]);
                return;
            }
        }
        i1 = i2 = 0;
        factor = 0.0f;
    }

    static inline bool has_range(const Range& r) { return r.vmin >= 0.0f && r.vmax >= 0.0f; }

    FlapSymbolResult get_optimal_flap(float gewicht_kg, float geschwindigkeit_kmh)
    {
        if (has_range(kLowSpeedRange) &&
            geschwindigkeit_kmh >= kLowSpeedRange.vmin &&
            geschwindigkeit_kmh <= kLowSpeedRange.vmax)
        {
            return {find_flap_index_by_symbol(kLowSpeedWk)};
        }

        if (kBereiche.empty() || kWeights.empty()) return {-1};

        int i1 = 0, i2 = 0;
        float f = 0.0f;
        weight_bracket(gewicht_kg, i1, i2, f);

        for (std::size_t idx = 0; idx < kBereiche.size(); ++idx)
        {
            const auto& b = kBereiche[idx];
            if (b.ranges.size() <= static_cast<std::size_t>(std::max(i1, i2))) continue;
            const Range r1 = b.ranges[i1];
            const Range r2 = b.ranges[i2];
            if (has_range(r1) && has_range(r2))
            {
                const float vmin = r1.vmin + f * (r2.vmin - r1.vmin);
                const float vmax = r1.vmax + f * (r2.vmax - r1.vmax);
                if (geschwindigkeit_kmh >= vmin && geschwindigkeit_kmh <= vmax)
                    return {
                        static_cast<int>(idx)
                    };
            }
            else if (has_range(r1))
            {
                if (geschwindigkeit_kmh >= r1.vmin && geschwindigkeit_kmh <= r1.vmax)
                    return {
                        static_cast<int>(idx)
                    };
            }
            else if (has_range(r2))
            {
                if (geschwindigkeit_kmh >= r2.vmin && geschwindigkeit_kmh <= r2.vmax)
                    return {
                        static_cast<int>(idx)
                    };
            }
        }
        return {-1};
    }

    std::vector<FlapSpeedRange> get_flap_speed_ranges(float gewicht_kg)
    {
        std::vector<FlapSpeedRange> result;
        if (kBereiche.empty() || kWeights.empty()) return result;

        int i1 = 0, i2 = 0;
        float f = 0.0f;
        weight_bracket(gewicht_kg, i1, i2, f);

        result.reserve(kBereiche.size());
        for (std::size_t idx = 0; idx < kBereiche.size(); ++idx)
        {
            const auto& b = kBereiche[idx];
            if (b.ranges.size() <= static_cast<std::size_t>(std::max(i1, i2))) continue;
            
            const Range r1 = b.ranges[i1];
            const Range r2 = b.ranges[i2];
            
            float vmin = -1.0f;
            float vmax = -1.0f;

            if (has_range(r1) && has_range(r2))
            {
                vmin = static_cast<float>(r1.vmin + f * (r2.vmin - r1.vmin));
                vmax = static_cast<float>(r1.vmax + f * (r2.vmax - r1.vmax));
            }
            else if (has_range(r1))
            {
                vmin = static_cast<float>(r1.vmin);
                vmax = static_cast<float>(r1.vmax);
            }
            else if (has_range(r2))
            {
                vmin = static_cast<float>(r2.vmin);
                vmax = static_cast<float>(r2.vmax);
            }

            result.push_back({
                static_cast<int>(idx),
                vmin,
                vmax
            });
        }
        return result;
    }

    const char* get_flap_symbol_name(int index)
    {
        if (index < 0 || static_cast<std::size_t>(index) >= kFlapTable.size()) return nullptr;
        return kFlapTable[index].symbol.c_str();
    }

    const char* get_range_symbol_name(int index)
    {
        if (index < 0 || static_cast<std::size_t>(index) >= kBereiche.size()) return nullptr;
        return kBereiche[index].wk.c_str();
    }
} // namespace flaputils
