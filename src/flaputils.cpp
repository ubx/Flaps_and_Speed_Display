#include "flaputils.hpp"

#include <string>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <dirent.h>
#include <cstring>
#if __has_include(<cjson/cJSON.h>)
#include <cjson/cJSON.h>
#else
#include "cJSON.h"
#endif
#ifndef NATIVE_TEST_BUILD
#include "nvs_flash.h"
#include "nvs.h"
#endif

#ifdef NATIVE_TEST_BUILD
#define NVS_SIMULATION_FILE ".nvs_simulation"
#endif

namespace flaputils
{
    struct FlapEntry
    {
        std::string symbol;
    };

    static std::vector<FlapEntry> kFlapTable;
    static std::vector<int> kWeights;
    static float kEmptyMassKg = 0.0f;
    static std::string kLowSpeedWk;
    static std::string kCurrentPolar;

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

        // Extract filename from path
        std::string path(filepath);
        size_t last_slash = path.find_last_of("/\\");
        if (last_slash != std::string::npos)
        {
            kCurrentPolar = path.substr(last_slash + 1);
        }
        else
        {
            kCurrentPolar = path;
        }

        // 1. meta (previously partially in speedpolar)
        if (const cJSON* meta = cJSON_GetObjectItem(root, "meta"))
        {
            if (cJSON* em = cJSON_GetObjectItem(meta, "empty_mass_kg")) kEmptyMassKg = static_cast<float>(em->valuedouble);
        }

        // 2. weights
        if (const cJSON* w_arr = cJSON_GetObjectItem(root, "weights"))
        {
            if (cJSON_IsArray(w_arr))
            {
                int sz = cJSON_GetArraySize(w_arr);
                for (int i = 0; i < sz; i++)
                {
                    kWeights.push_back(cJSON_GetArrayItem(w_arr, i)->valueint);
                }
            }
        }

        // 3. flaps (previously flap2symbol)
        if (const cJSON* flaps = cJSON_GetObjectItem(root, "flaps"))
        {
            cJSON* lab_arr = cJSON_GetObjectItem(flaps, "labels");
            if (lab_arr && cJSON_IsArray(lab_arr))
            {
                int sz = cJSON_GetArraySize(lab_arr);
                for (int i = 0; i < sz; i++)
                {
                    cJSON* lab_item = cJSON_GetArrayItem(lab_arr, i);
                    if (lab_item && lab_item->valuestring)
                    {
                        kFlapTable.push_back({lab_item->valuestring});
                    }
                }
            }
        }

        // 4. speedpolar
        if (const cJSON* sp_arr = cJSON_GetObjectItem(root, "speedpolar"))
        {
            if (cJSON_IsArray(sp_arr))
            {
                int b_sz = cJSON_GetArraySize(sp_arr);
                for (int i = 0; i < b_sz; i++)
                {
                    cJSON* b_item = cJSON_GetArrayItem(sp_arr, i);
                    Bereich b;
                    cJSON* wk = cJSON_GetObjectItem(b_item, "wk");
                    if (wk && wk->valuestring) b.wk = wk->valuestring;

                    if (cJSON* r_arr_outer = cJSON_GetObjectItem(b_item, "ranges"))
                    {
                        if (cJSON_IsArray(r_arr_outer))
                        {
                            int r_sz = cJSON_GetArraySize(r_arr_outer);
                            for (int j = 0; j < r_sz; j++)
                            {
                                cJSON* r_pair = cJSON_GetArrayItem(r_arr_outer, j);
                                if (cJSON_IsArray(r_pair) && cJSON_GetArraySize(r_pair) >= 2)
                                {
                                    b.ranges.push_back({
                                        static_cast<float>(cJSON_GetArrayItem(r_pair, 0)->valuedouble),
                                        static_cast<float>(cJSON_GetArrayItem(r_pair, 1)->valuedouble)
                                    });
                                }
                            }
                        }
                    }
                    kBereiche.push_back(b);
                }
            }
        }

        // 5. lowspeed
        if (const cJSON* ls = cJSON_GetObjectItem(root, "lowspeed"))
        {
            if (const cJSON* wk = cJSON_GetObjectItem(ls, "wk"); wk && wk->valuestring)
            {
                kLowSpeedWk = wk->valuestring;
            }

            if (const cJSON* r_pair = cJSON_GetObjectItem(ls, "range"))
            {
                if (cJSON_IsArray(r_pair) && cJSON_GetArraySize(r_pair) >= 2)
                {
                    kLowSpeedRange = {
                        static_cast<float>(cJSON_GetArrayItem(r_pair, 0)->valuedouble),
                        static_cast<float>(cJSON_GetArrayItem(r_pair, 1)->valuedouble)
                    };
                }
            }
        }

        cJSON_Delete(root);
        free(buffer);
        return true;
    }

    float get_empty_mass() { return kEmptyMassKg; }

    FlapSymbolResult get_flap_symbol(int flapIdx)
    {
        if (flapIdx >= 0 && static_cast<std::size_t>(flapIdx) < kFlapTable.size())
        {
            return {flapIdx};
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

    const char* get_polar()
    {
        return kCurrentPolar.c_str();
    }

    bool save_polar_path(const char* filepath)
    {
#ifndef NATIVE_TEST_BUILD
        nvs_handle_t my_handle;
        esp_err_t err = nvs_open("storage", NVS_READWRITE, &my_handle);
        if (err != ESP_OK) return false;

        err = nvs_set_str(my_handle, "polar_path", filepath);
        if (err == ESP_OK) {
            err = nvs_commit(my_handle);
        }
        nvs_close(my_handle);
        return (err == ESP_OK);
#else
        FILE* f = fopen(NVS_SIMULATION_FILE, "w");
        if (!f) return false;
        fprintf(f, "%s", filepath);
        fclose(f);
        return true;
#endif
    }

    bool load_persisted_data()
    {
#ifndef NATIVE_TEST_BUILD
        nvs_handle_t my_handle;
        esp_err_t err = nvs_open("storage", NVS_READONLY, &my_handle);
        if (err != ESP_OK) return false;

        char path[256];
        size_t required_size = sizeof(path);
        err = nvs_get_str(my_handle, "polar_path", path, &required_size);
        nvs_close(my_handle);

        if (err == ESP_OK) {
            return load_data(path);
        }
        return false;
#else
        FILE* f = fopen(NVS_SIMULATION_FILE, "r");
        if (!f) return false;
        char path[256];
        bool success = false;
        if (fgets(path, sizeof(path), f)) {
            // Remove trailing newline if any
            path[strcspn(path, "\r\n")] = 0;
            success = load_data(path);
        }
        fclose(f);
        return success;
#endif
    }

    std::string find_first_polar_path()
    {
#ifdef NATIVE_TEST_BUILD
        const char* dir_path = "spiffs_data";
#else
        const char* dir_path = "/spiffs";
#endif
        DIR* dir = opendir(dir_path);
        if (!dir) return "";

        struct dirent* ent;
        std::string first_file;
        while ((ent = readdir(dir)) != nullptr)
        {
            if (ent->d_name[0] == '.') continue;
            std::string filename = ent->d_name;
            if (filename.length() > 5 && filename.substr(filename.length() - 5) == ".json")
            {
                //if (filename == "ventus3_defaut.json") continue;
                first_file = std::string(dir_path) + "/" + filename;
                break;
            }
        }
        closedir(dir);
        return first_file;
    }
} // namespace flaputils
