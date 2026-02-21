#ifndef NATIVE_TEST_BUILD
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <mutex>
#include <map>
#include <string>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/twai.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#ifdef ENABLE_DIAGNOSTICS
#include "esp_partition.h"
#include <dirent.h>
#endif

#include "flaputils.hpp"
#include "ui/screens/screen1.hpp"

static const char* TAG = "CANReceiver";

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

    void update_float(const std::string& key, float value)
    {
        std::lock_guard<std::mutex> lock(mtx);
        if (key == "ias") ias = value;
        else if (key == "tas") tas = value;
        else if (key == "cas") cas = value;
        else if (key == "alt") alt = value;
        else if (key == "vario") vario = value;
        else if (key == "gs") gs = value;
        else if (key == "tt") tt = value;
    }

    void update_double(const std::string& key, double value)
    {
        std::lock_guard<std::mutex> lock(mtx);
        if (key == "lat") lat = value;
        else if (key == "lon") lon = value;
    }

    void update_int(const std::string& key, int value)
    {
        std::lock_guard<std::mutex> lock(mtx);
        if (key == "flap") flap = value;
    }

    void update_uint16(const std::string& key, uint16_t value)
    {
        std::lock_guard<std::mutex> lock(mtx);
        if (key == "dry_and_ballast_mass") dry_and_ballast_mass = value;
        else if (key == "enl") enl = value;
    }

    void print()
    {
#ifndef ENABLE_DIAGNOSTICS
        std::lock_guard<std::mutex> lock(mtx);
        printf(
            "FlightData: IAS=%.2f, TAS=%.2f, CAS=%.2f, ALT=%.2f, Vario=%.2f, Flap=%d, Lat=%.7f, Lon=%.7f, GS=%.2f, TT=%.2f, Dry + Ballast Mass=%u, ENL=%u\n",
            ias * 3.6, tas, cas, alt, vario, flap, lat, lon, gs, tt, dry_and_ballast_mass / 10, enl);

        const flaputils::FlapSymbolResult optimal = flaputils::get_optimal_flap(
            (dry_and_ballast_mass / 10.0) + 84, ias * 3.6);
        const flaputils::FlapSymbolResult actual = flaputils::get_flap_symbol(flap);
        printf("Flaps: Optimal=%s, Actual=%s\n",
               optimal.symbol ? optimal.symbol : "N/A",
               actual.symbol ? actual.symbol : "N/A");
#endif
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
        xTaskCreatePinnedToCore(receive_task, "can_rx_task", 4096, this, 5, NULL, tskNO_AFFINITY);
    }

private:
    FlightData& flight_data;

    static void receive_task(void* arg)
    {
        CANReceiver* self = (CANReceiver*)arg;
        self->run();
    }

    void run()
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

    void handle_message(const twai_message_t& msg)
    {
        if (!(msg.flags & TWAI_MSG_FLAG_EXTD))
        {
            // Standard Frame
            uint32_t id = msg.identifier;
            switch (id)
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

    float get_float(const uint8_t* data)
    {
        uint32_t raw = __builtin_bswap32(*(const uint32_t*)(data + 4));
        return std::bit_cast<float>(raw); // C++20
    }

    double get_double_l(const uint8_t* data)
    {
        uint32_t raw = __builtin_bswap32(*(const uint32_t*)(data + 4));
        return (int32_t)raw / 1E7;
    }

    uint16_t get_ushort(const uint8_t* data)
    {
        return __builtin_bswap16(*(const uint16_t*)(data + 4));
    }

    int get_char(const uint8_t* data)
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

flaputils::FlapSymbolResult get_flap_actual()
{
    std::lock_guard<std::mutex> lock(flight_state.mtx);
    return flaputils::get_flap_symbol(flight_state.flap);
}


flaputils::FlapSymbolResult get_flap_target()
{
    std::lock_guard<std::mutex> lock(flight_state.mtx);
    double weight = (flight_state.dry_and_ballast_mass / 10.0) + 84.0;
    return flaputils::get_optimal_flap(weight, flight_state.ias * 3.6);
}

void print_task(void* arg)
{
    FlightData* data = (FlightData*)arg;
    int cnt = 0;
    while (true)
    {
        data->print();

        cnt++;
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

    // Initialize TWAI driver
    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT((gpio_num_t)TWAI_TX_GPIO, (gpio_num_t)TWAI_RX_GPIO,
                                                                 TWAI_MODE_NORMAL);
    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    if (twai_driver_install(&g_config, &t_config, &f_config) == ESP_OK)
    {
        if (twai_start() == ESP_OK)
        {
            ESP_LOGI(TAG, "TWAI Driver started");
            receiver.start();
            xTaskCreate(print_task, "print_task", 4096, &flight_state, 2, NULL);
            screen1_start();
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
