#ifndef NATIVE_TEST_BUILD
#include <cstdio>
#include <cstdint>
#include <mutex>
#include <string>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/twai.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#ifdef ENABLE_DIAGNOSTICS
#include "esp_partition.h"
#include <dirent.h>
#endif

#include "flaputils.hpp"
#include "ble_ota.hpp"
#include "ui/ui.h"
#include "ui/screens/screen2.hpp"
#ifndef NATIVE_TEST_BUILD
#include "bsp/esp32_s3_touch_amoled_1_75.h"
#include "lvgl.h"
#endif

#define APP_NAME "Flaps & Speed"
#define APP_VERSION "1.0.0"

#ifndef GIT_REVISION
#define GIT_REVISION "unknown"
#endif

static const char* TAG = "main";
static constexpr uint64_t STALE_TIMEOUT_MS = 10000ULL;

struct FlightData
{
    std::mutex mtx;
    float ias = 0;
    float tas = 0;
    float cas = 0;
    float alt = 0;
    float vario = 0;
    int flap = 0;
    double lat = 0;
    double lon = 0;
    float gs = 0;
    float tt = 0;
    uint16_t dry_and_ballast_mass = 0;
    uint16_t enl = 0;
    uint64_t last_relevant_rx_ms = 0;

    static uint64_t monotonic_ms()
    {
        return esp_timer_get_time() / 1000ULL;
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
        else if (key == "cas") cas = value;
        else if (key == "alt") alt = value;
        else if (key == "vario") vario = value;
        else if (key == "gs")
        {
            gs = value;
            last_relevant_rx_ms = monotonic_ms();
        }
        else if (key == "tt") tt = value;
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

    void print()
    {
#ifndef ENABLE_DIAGNOSTICS
        std::lock_guard lock(mtx);
        printf(
            "FlightData: IAS=%.2f, TAS=%.2f, CAS=%.2f, ALT=%.2f, Vario=%.2f, Flap=%d, Lat=%.7f, Lon=%.7f, GS=%.2f, TT=%.2f, Dry + Ballast Mass=%u, ENL=%u\n",
            ias * 3.6, tas, cas, alt, vario, flap, lat, lon, gs, tt, dry_and_ballast_mass / 10, enl);

        const auto [index] = flaputils::get_optimal_flap(
            dry_and_ballast_mass / 10 + 84, ias * 3.6f);
        const flaputils::FlapSymbolResult actual = flaputils::get_flap_symbol(flap);
        const char* opt_sym = flaputils::get_range_symbol_name(index);
        const char* act_sym = flaputils::get_flap_symbol_name(actual.index);
        printf("Flaps: Optimal=%s, Actual=%s\n",
               opt_sym ? opt_sym : "N/A",
               act_sym ? act_sym : "N/A");
#endif
    }

    bool is_stale()
    {
        std::lock_guard lock(mtx);
        const uint64_t now_ms = monotonic_ms();
        if (last_relevant_rx_ms == 0)
        {
            return now_ms >= STALE_TIMEOUT_MS;
        }
        return (now_ms - last_relevant_rx_ms) >= STALE_TIMEOUT_MS;
    }
};

class CANReceiver
{
public:
    CANReceiver(FlightData& flight_data) : flight_data(flight_data)
    {
    }

    void start()
    {
        xTaskCreatePinnedToCore(receive_task, "can_rx_task", 4096, this, 5, nullptr, tskNO_AFFINITY);
    }

private:
    FlightData& flight_data;

    static void receive_task(void* arg)
    {
        auto self = static_cast<CANReceiver*>(arg);
        self->run();
    }

    [[noreturn]] void run() const
    {
        twai_message_t message;
        while (true)
        {
            if (twai_receive(&message, pdMS_TO_TICKS(1000)) == ESP_OK)
            {
                handle_message(message);
            }
        }
    }

    void handle_message(const twai_message_t& msg) const
    {
        if (!(msg.flags & TWAI_MSG_FLAG_EXTD))
        {
            // Standard Frame
            switch (msg.identifier)
            {
            case 315: flight_data.update_float("ias", get_float(msg.data));
                break;
            case 316: flight_data.update_float("tas", get_float(msg.data));
                break;
            case 317: flight_data.update_float("cas", get_float(msg.data));
                break;
            case 322: flight_data.update_float("alt", get_float(msg.data));
                break;
            case 340: flight_data.update_int("flap", get_char(msg.data));
                break;
            case 354: flight_data.update_float("vario", get_float(msg.data));
                break;
            case 1036: flight_data.update_double("lat", get_double_l(msg.data));
                break;
            case 1037: flight_data.update_double("lon", get_double_l(msg.data));
                break;
            case 1039: flight_data.update_float("gs", get_float(msg.data));
                break;
            case 1040: flight_data.update_float("tt", get_float(msg.data));
                break;
            case 1515: flight_data.update_uint16("dry_and_ballast_mass", get_ushort(msg.data));
                // glide polar mass dry and ballast [ushort kg*10^1]
                break;
            case 1506: flight_data.update_uint16("enl", get_ushort(msg.data));
                break;
            default:
                // Unknown ID
                break;
            }
        }
    }

    static float get_float(const uint8_t* data)
    {
        uint32_t raw = __builtin_bswap32(*reinterpret_cast<const uint32_t*>(data + 4));
        return std::bit_cast<float>(raw); // C++20
    }

    static double get_double_l(const uint8_t* data)
    {
        uint32_t raw = __builtin_bswap32(*reinterpret_cast<const uint32_t*>(data + 4));
        return static_cast<int32_t>(raw) / 1E7;
    }

    static uint16_t get_ushort(const uint8_t* data)
    {
        return __builtin_bswap16(*reinterpret_cast<const uint16_t*>(data + 4));
    }

    static int get_char(const uint8_t* data)
    {
        return (int)data[4];
    }
};

static FlightData flight_state;
static CANReceiver receiver(flight_state);

float get_ias_kmh()
{
    std::lock_guard<std::mutex> lock(flight_state.mtx);
    return flight_state.ias * 3.6f;
}

float get_weight_kg()
{
    std::lock_guard<std::mutex> lock(flight_state.mtx);
    return flight_state.dry_and_ballast_mass / 10 + 84;
}

flaputils::FlapSymbolResult get_flap_actual()
{
    std::lock_guard<std::mutex> lock(flight_state.mtx);
    return flaputils::get_flap_symbol(flight_state.flap);
}


flaputils::FlapSymbolResult get_flap_target()
{
    float weight = get_weight_kg();
    std::lock_guard<std::mutex> lock(flight_state.mtx);
    return flaputils::get_optimal_flap(weight, flight_state.ias * 3.6f);
}

bool is_stale()
{
    return flight_state.is_stale();
}

[[noreturn]] void print_task(void* arg)
{
    auto data = static_cast<FlightData*>(arg);
    while (true)
    {
        data->print();

#if defined(ENABLE_DIAGNOSTICS) && (configGENERATE_RUN_TIME_STATS == 1) && (configUSE_STATS_FORMATTING_FUNCTIONS == 1)
        if (cnt % 10 == 0)
        {
            char stats[512];
            vTaskGetRunTimeStats(stats);
            printf("Task runtime stats:\n%s\n", stats);
        }
#endif

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

extern "C" void app_main(void)
{
    vTaskDelay(pdMS_TO_TICKS(2000));
    // Initialize SPIFFS

#ifdef ENABLE_DIAGNOSTICS
    // Check if the partition exists
    const esp_partition_t* partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                                                ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "spiffs");
    if (partition == NULL)
    {
        ESP_LOGE(TAG, "Failed to find SPIFFS partition labeled 'spiffs' in the partition table!");
        // List all data partitions for debugging
        esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, NULL);
        while (it != NULL)
        {
            const esp_partition_t* p = esp_partition_get(it);
            ESP_LOGI(TAG, "Found data partition: label=%s, type=%d, subtype=%d, offset=0x%lx, size=0x%lx",
                     p->label, p->type, p->subtype, (unsigned long)p->address, (unsigned long)p->size);
            it = esp_partition_next(it);
        }
    }
    else
    {
        ESP_LOGI(TAG, "Found SPIFFS partition at offset 0x%lx, size 0x%lx", (unsigned long)partition->address,
                 (unsigned long)partition->size);
    }
#endif

    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = "spiffs",
        .max_files = 5,
        .format_if_mount_failed = false
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK)
    {
        if (ret == ESP_FAIL)
        {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        }
        else if (ret == ESP_ERR_NOT_FOUND)
        {
            ESP_LOGE(TAG, "Failed to find SPIFFS partition");
        }
        else
        {
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
    }
    else
    {
        size_t total = 0, used = 0;
        ret = esp_spiffs_info("spiffs", &total, &used);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s)", esp_err_to_name(ret));
        }
        else
        {
            ESP_LOGI(TAG, "Partition size: total: %u, used: %u", (unsigned int)total, (unsigned int)used);
            if (used == 0)
            {
                ESP_LOGW(TAG, "SPIFFS partition is empty! Did you run 'pio run -t uploadfs'?");
            }
        }

#ifdef ENABLE_DIAGNOSTICS
        ESP_LOGI(TAG, "Listing files in /spiffs:");
        DIR* dir = opendir("/spiffs");
        if (dir)
        {
            struct dirent* ent;
            while ((ent = readdir(dir)) != NULL)
            {
                ESP_LOGI(TAG, "Found file: %s", ent->d_name);
            }
            closedir(dir);
        }
        else
        {
            ESP_LOGE(TAG, "Failed to open /spiffs directory");
        }
#endif

        if (flaputils::load_data("/spiffs/flapDescriptor.json"))
        {
            ESP_LOGI(TAG, "Flap data loaded successfully");
        }
        else
        {
            ESP_LOGE(TAG, "Failed to load flap data from SPIFFS");
        }
    }

    ret = ble_ota_init();
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "BLE OTA init skipped: %s", esp_err_to_name(ret));
    }

    // Initialize TWAI driver
    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(static_cast<gpio_num_t>(TWAI_TX_GPIO),
        static_cast<gpio_num_t>(TWAI_RX_GPIO),TWAI_MODE_NORMAL);
    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    if (twai_driver_install(&g_config, &t_config, &f_config) == ESP_OK)
    {
        if (twai_start() == ESP_OK)
        {
            ESP_LOGI(TAG, "TWAI Driver started");
            receiver.start();
            xTaskCreate(print_task, "print_task", 4096, &flight_state, 2, nullptr);
            ui_init();

            // show name, version and git rev
            set_label1(APP_NAME);
            set_label2("Version: " APP_VERSION);
            set_label3("Git Rev: " GIT_REVISION);
            vTaskDelay(pdMS_TO_TICKS(5000));

            // Load screen2 after splash
            if (bsp_display_lock(-1) == ESP_OK)
            {
                lv_screen_load_anim(screen2_get(), LV_SCR_LOAD_ANIM_FADE_ON, 500, 0, false);
                bsp_display_unlock();
            }
        }
        else
        {
            ESP_LOGE(TAG, "Failed to start TWAI driver");
        }
    }
    else
    {
        ESP_LOGE(TAG, "Failed to install TWAI driver");
    }
}
#endif // NATIVE_TEST_BUILD
