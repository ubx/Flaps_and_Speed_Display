#include <stdio.h>
#include <string>
#include <vector>
#include "../src/flaputils.hpp"

// PlatformIO/ESP-IDF test entry point is often app_main or similar, 
// but for a standalone "as in __main__" test, we can provide a function 
// that can be called from main or used as a test file.
// Since it's ESP-IDF, we'll use app_main if it's meant to run on the device,
// but usually, simple prints go to stdout.

void run_tests()
{
    using namespace flaputils;

    const char* candidates[] = {
        "spiffs_data/flapDescriptor.json", "flapDescriptor.json", "/spiffs/flapDescriptor.json"
    };
    bool loaded = false;
    for (const char* p : candidates)
    {
        if (load_data(p))
        {
            printf("Loaded flap data from %s\n", p);
            loaded = true;
            break;
        }
    }
    if (!loaded)
    {
        printf("WARNING: Failed to load flapDescriptor.json. Functions may return no data.\n");
    }

    printf("Empty Mass (function): %.2f kg\n", get_empty_mass());

    printf("\n--- Testing get_flap_symbol ---\n");
    int test_positions[] = {94, 95, 96, 97, 84, 85, 250, 252, 0, 230, 157, 167};
    for (int pos : test_positions)
    {
        FlapSymbolResult res = get_flap_symbol(pos);
        printf("Position %d -> Symbol: %s, Index: %d\n",
               pos, res.symbol ? res.symbol : "None", res.index);
    }

    printf("\n--- Testing get_optimal_flap (Interpolation) ---\n");
    struct TestCase
    {
        double w;
        double v;
        const char* expected;
        int expected_index;
    };

    std::vector<TestCase> test_cases = {
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

    for (const auto& tc : test_cases)
    {
        flaputils::FlapSymbolResult res = get_optimal_flap(tc.w, tc.v);
        bool ok = false;
        if (res.symbol && tc.expected && std::string(res.symbol) == std::string(tc.expected) && res.index == tc.
            expected_index)
        {
            ok = true;
        }
        else if (!res.symbol && !tc.expected && res.index == -1)
        {
            ok = true;
        }

        printf("Weight %.0fkg, Speed %.0fkm/h -> Optimal Flap: %s, Index: %d (Expected: %s, %d) %s\n",
               tc.w, tc.v, res.symbol ? res.symbol : "None", res.index,
               tc.expected ? tc.expected : "None", tc.expected_index, ok ? "OK" : "NOK");
    }
}

#ifdef NATIVE_BUILD
int main()
{
    run_tests();
    return 0;
}
#else
extern "C" void app_main(void)
{
    run_tests();
}
#endif
