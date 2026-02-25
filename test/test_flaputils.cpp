#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include "../src/flaputils.hpp"

static int run_tests()
{
    using namespace flaputils;

    bool loaded = false;

#ifdef NATIVE_TEST_BUILD
    const char* candidates[] = {
        "spiffs_data/flapDescriptor.json",
        "flapDescriptor.json",
        "/spiffs/flapDescriptor.json"
    };
#else
    // On-device: usually only the mounted FS path is meaningful
    const char* candidates[] = {
        "/spiffs/flapDescriptor.json",
        "flapDescriptor.json"
    };
#endif

    for (const char* p : candidates)
    {
        if (load_data(p))
        {
            std::printf("Loaded flap data from %s\n", p);
            loaded = true;
            break;
        }
    }

    if (!loaded)
    {
        std::printf("WARNING: Failed to load flapDescriptor.json. Functions may return no data.\n");
    }

    int fails = 0;

    if (loaded)
        std::printf("Empty Mass: %.2f kg\n", get_empty_mass());
    else
        std::printf("Empty Mass: (unavailable - data not loaded)\n");

    std::printf("\n--- Testing get_flap_symbol ---\n");
    {
        int test_positions[] = {94, 95, 96, 97, 84, 85, 250, 252, 0, 230, 157, 167};
        for (int pos : test_positions)
        {
            FlapSymbolResult res = get_flap_symbol(pos);
            std::printf("Position %d -> Symbol: %s, Index: %d\n",
                        pos, res.symbol ? res.symbol : "None", res.index);
        }
    }

    std::printf("\n--- Testing get_flap_speed_ranges ---\n");
    {
        auto params = get_flap_speed_ranges(get_empty_mass());

        if (!loaded)
        {
            if (!params.empty())
            {
                ++fails;
                std::printf("NOK: Data not loaded but get_flap_speed_ranges returned %zu entries (expected 0)\n",
                            params.size());
            }
            else
            {
                std::printf("OK: Data not loaded and get_flap_speed_ranges returned 0 entries\n");
            }
        }
        else
        {
            bool ok = !params.empty();
            if (!ok)
            {
                ++fails;
                std::printf("NOK: get_flap_speed_ranges returned 0 entries\n");
            }

            for (std::size_t i = 0; i < params.size(); ++i)
            {
                const bool entry_ok = (params[i].symbol != nullptr) && (params[i].index >= 0);
                if (!entry_ok) ok = false;

                std::printf("Param[%zu] -> Symbol: %s, Index: %d %s\n",
                            i,
                            params[i].symbol ? params[i].symbol : "None",
                            params[i].index,
                            entry_ok ? "OK" : "NOK");
            }

            if (!ok) ++fails;
            std::printf("get_flap_speed_ranges overall: %s (count=%zu)\n", ok ? "OK" : "NOK", params.size());
        }
    }

    struct TestCase
    {
        double w;
        double v;
        const char* expected;
        int expected_index;
    };

    const std::vector<TestCase> test_cases = {
        {390, 70, "L", 0},
        {390, 85, "+1", 2},
        {430, 81, "+2", 1},
        {600, 100, "+1", 2},
        {410, 78, "L", 0},
        {410, 79, "+2", 1},
        {500, 130, "0", 3},
        {580, 270, "S1", 7},
        {580, 70, "L", 0}
    };

    std::printf("\n--- Testing get_optimal_flap (Interpolation) ---\n");
    {
        for (const auto& tc : test_cases)
        {
            FlapSymbolResult res = get_optimal_flap(tc.w, tc.v);

            bool ok = false;
            if (tc.expected)
            {
                ok = (res.symbol != nullptr) &&
                    (std::strcmp(res.symbol, tc.expected) == 0) &&
                    (res.index == tc.expected_index);
            }
            else
            {
                ok = (res.symbol == nullptr) && (res.index == -1);
            }

            if (!ok) ++fails;

            std::printf("Weight %.0fkg, Speed %.0fkm/h -> Optimal Flap: %s, Index: %d (Expected: %s, %d) %s\n",
                        tc.w, tc.v,
                        res.symbol ? res.symbol : "None", res.index,
                        tc.expected ? tc.expected : "None", tc.expected_index,
                        ok ? "OK" : "NOK");
        }
    }

    std::printf("\n--- Testing get_flap_speed_ranges (Interpolation) ---\n");
    {
        if (!loaded)
        {
            auto ranges = get_flap_speed_ranges(450.0);
            if (!ranges.empty())
            {
                ++fails;
                std::printf("NOK: Data not loaded but get_flap_speed_ranges returned %zu entries (expected 0)\n",
                            ranges.size());
            }
            else
            {
                std::printf("OK: Data not loaded and get_flap_speed_ranges returned 0 entries\n");
            }
        }
        else
        {
            // Collect unique weights from test_cases
            std::vector<double> unique_weights;
            for (const auto& tc : test_cases)
            {
                bool found = false;
                for (double uw : unique_weights)
                {
                    if (std::abs(uw - tc.w) < 0.1)
                    {
                        found = true;
                        break;
                    }
                }
                if (!found) unique_weights.push_back(tc.w);
            }

            for (double test_weight : unique_weights)
            {
                auto ranges = get_flap_speed_ranges(test_weight);
                std::printf("Weight %.1f kg:\n", test_weight);
                for (const auto& r : ranges)
                {
                    std::printf("  Symbol: %4s, Index: %d, Range: [%6.1f, %6.1f]\n",
                                r.symbol ? r.symbol : "None", r.index, r.lower_speed, r.upper_speed);
                }

                // Basic sanity check: ensure we got some data
                if (ranges.empty())
                {
                    ++fails;
                    std::printf("NOK: get_flap_speed_ranges returned 0 entries for weight %.1f\n", test_weight);
                }
                else
                {
                    std::printf("OK: get_flap_speed_ranges returned %zu entries\n", ranges.size());
                }
            }
        }
    }

    std::printf("\n=== TEST SUMMARY: %s (fails=%d) ===\n", (fails == 0) ? "PASS" : "FAIL", fails);
    return fails;
}

#ifdef NATIVE_TEST_BUILD
int main()
{
    return run_tests();
}
#else
extern "C" void app_main(void)
{
    (void)run_tests();
}
#endif
