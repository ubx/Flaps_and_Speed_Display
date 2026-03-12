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
#include "esp_task_wdt.h"
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
    float alt = 0;
    float vario = 0;
    int flap = 0;
    double lat = 0;
    double lon = 0;
    float gs = 0;
    float tt = 0;
    uint16_t dry_and_ballast_mass = 0;
    uint16_t enl = 0;
    float wind_speed = 0;
    float wind_direction = 0;
    float heading = 0;
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
        else if (key == "alt") alt = value;
        else if (key == "vario") vario = value;
        else if (key == "gs")
        {
            gs = value;
            last_relevant_rx_ms = monotonic_ms();
        }
        else if (key == "tt") tt = value;
        else if (key == "wind_speed") wind_speed = value;
        else if (key == "wind_direction") wind_direction = value;
        else if (key == "heading") heading = value;
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
            "FlightData: IAS=%.2f, TAS=%.2f, ALT=%.2f, Vario=%.2f, Flap=%d, Lat=%.7f, Lon=%.7f, GS=%.2f, TT=%.2f, Dry + Ballast Mass=%u, ENL=%u, Wind Speed=%.2f, Wind Dir=%.2f, Heading=%.2f\n",
            ias * 3.6, tas * 3.6, alt, vario, flap, lat, lon, gs, tt, dry_and_ballast_mass / 10, enl, wind_speed, wind_direction, heading);

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
            case 321: flight_data.update_float("heading", get_float(msg.data));
                break;
            case 322: flight_data.update_float("alt", get_float(msg.data));
                break;
            case 333: flight_data.update_float("wind_speed", get_float(msg.data));
                break;
            case 334: flight_data.update_float("wind_direction", get_float(msg.data));
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

static void configure_task_wdt_for_ui(void)
{
#if CONFIG_ESP_TASK_WDT_EN && !CONFIG_FREERTOS_UNICORE
    esp_task_wdt_config_t twdt_cfg = {
        .timeout_ms = CONFIG_ESP_TASK_WDT_TIMEOUT_S * 1000,
        .idle_core_mask = (1U << 0), // monitor CPU0 idle only
#if CONFIG_ESP_TASK_WDT_PANIC
        .trigger_panic = true,
#else
        .trigger_panic = false,
#endif
    };

    esp_err_t ret = esp_task_wdt_reconfigure(&twdt_cfg);
    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "Reconfigured TWDT to monitor IDLE0 only");
        return;
    }

    // Fallback path if reconfigure is unavailable/blocked
    TaskHandle_t idle1 = xTaskGetIdleTaskHandleForCore(1);
    if (!idle1)
    {
        ESP_LOGW(TAG, "IDLE1 handle unavailable; TWDT reconfigure failed: %s", esp_err_to_name(ret));
        return;
    }

    ret = esp_task_wdt_delete(idle1);
    if (ret == ESP_OK || ret == ESP_ERR_NOT_FOUND)
    {
        ESP_LOGI(TAG, "Cleared IDLE1 watchdog subscription");
    }
    else
    {
        ESP_LOGW(TAG, "Failed to clear IDLE1 watchdog subscription: %s", esp_err_to_name(ret));
    }
#endif
}

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
    configure_task_wdt_for_ui();
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
#else
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>

#include "flaputils.hpp"
#include "lvgl.h"
#include "platform/ui_platform.hpp"
#include "ui/ui.h"
#include "ui/screens/screen1.hpp"
#include "ui/screens/screen2.hpp"
#include "ui/screens/screen3.hpp"
#include "ui/screens/screen4.hpp"

#ifndef APP_NAME
#define APP_NAME "Flaps & Speed"
#define APP_VERSION "1.0.0"
#endif
#ifndef GIT_REVISION
#define GIT_REVISION "unknown"
#endif

static constexpr uint64_t STALE_TIMEOUT_MS = 10000ULL;

struct FlightData
{
    std::mutex mtx;
    float ias = 0;
    float tas = 0;
    float ias_mps = 0; // for compatibility with existing native code if needed, but we'll rename it
    float alt = 0;
    float vario = 0;
    int flap = 0;
    double lat = 0;
    double lon = 0;
    float gs = 0;
    float tt = 0;
    uint16_t dry_and_ballast_mass = 3800;
    uint16_t enl = 0;
    float wind_speed = 0;
    float wind_direction = 0;
    float heading = 0;
    uint64_t last_relevant_rx_ms = 0;

    static uint64_t monotonic_ms()
    {
        using namespace std::chrono;
        return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
    }

    void update(float ias_kmh, int flap_position, float total_weight_kg)
    {
        std::lock_guard lock(mtx);
        ias = ias_kmh / 3.6f;
        ias_mps = ias;
        flap = flap_position;
        const float dry_and_ballast = std::max(0.0f, total_weight_kg - 84.0f);
        dry_and_ballast_mass = static_cast<uint16_t>(std::lround(dry_and_ballast * 10.0f));
        last_relevant_rx_ms = monotonic_ms();
    }

    void print()
    {
        std::lock_guard lock(mtx);
        printf(
            "FlightData: IAS=%.2f, TAS=%.2f, ALT=%.2f, Vario=%.2f, Flap=%d, Lat=%.7f, Lon=%.7f, GS=%.2f, TT=%.2f, Dry + Ballast Mass=%u, ENL=%u, Wind Speed=%.2f, Wind Dir=%.2f, Heading=%.2f\n",
            ias * 3.6, tas * 3.6, alt, vario, flap, lat, lon, gs, tt, dry_and_ballast_mass / 10, enl, wind_speed, wind_direction, heading);

        const auto [index] = flaputils::get_optimal_flap(
            dry_and_ballast_mass / 10 + 84, ias * 3.6f);
        const flaputils::FlapSymbolResult actual = flaputils::get_flap_symbol(flap);
        const char* opt_sym = flaputils::get_range_symbol_name(index);
        const char* act_sym = flaputils::get_flap_symbol_name(actual.index);
        printf("Flaps: Optimal=%s, Actual=%s\n",
               opt_sym ? opt_sym : "N/A",
               act_sym ? act_sym : "N/A");
    }

    bool is_stale()
    {
        std::lock_guard lock(mtx);
        const uint64_t now_ms = monotonic_ms();
        if (last_relevant_rx_ms == 0) return now_ms >= STALE_TIMEOUT_MS;
        return (now_ms - last_relevant_rx_ms) >= STALE_TIMEOUT_MS;
    }
};

struct SimulatorConfig
{
    int start_screen = 2;
    bool auto_cycle = false;
    int cycle_seconds = 8;
    int splash_ms = 1500;
    std::string can_iface = "can0";
};

static FlightData g_flight_state;
static std::atomic<bool> g_running{true};

static void handle_signal(int)
{
    g_running = false;
}

static int parse_int_arg(const char* value, int fallback)
{
    return value ? std::atoi(value) : fallback;
}

static float parse_float_arg(const char* value, float fallback)
{
    return value ? std::strtof(value, nullptr) : fallback;
}

static SimulatorConfig parse_args(int argc, char** argv)
{
    SimulatorConfig cfg;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--screen") == 0 && i + 1 < argc)
        {
            cfg.start_screen = std::clamp(parse_int_arg(argv[++i], cfg.start_screen), 1, 4);
        }
        else if (std::strcmp(argv[i], "--auto-cycle") == 0)
        {
            cfg.auto_cycle = true;
        }
        else if (std::strcmp(argv[i], "--cycle-seconds") == 0 && i + 1 < argc)
        {
            cfg.cycle_seconds = std::max(1, parse_int_arg(argv[++i], cfg.cycle_seconds));
        }
        else if (std::strcmp(argv[i], "--speed") == 0 && i + 1 < argc)
        {
            ++i;
        }
        else if (std::strcmp(argv[i], "--can-iface") == 0 && i + 1 < argc)
        {
            cfg.can_iface = argv[++i];
        }
        else if (std::strcmp(argv[i], "--no-splash") == 0)
        {
            cfg.splash_ms = 0;
        }
        else if (std::strcmp(argv[i], "--help") == 0)
        {
            std::printf(
                "Usage: .pio/build/native/program [--screen 1..4] [--auto-cycle] [--cycle-seconds N] [--can-iface can0] [--no-splash]\n");
            std::exit(0);
        }
    }
    return cfg;
}

static lv_obj_t* get_screen_by_index(int screen)
{
    switch (screen)
    {
    case 1: return screen1_get();
    case 2: return screen2_get();
    case 3: return screen3_get();
    case 4: return screen4_get();
    default: return screen2_get();
    }
}

static void load_screen(int screen, lv_screen_load_anim_t anim)
{
    if (!ui_platform_lock(-1)) return;
    lv_obj_t* target = get_screen_by_index(screen);
    if (target)
    {
#ifdef NATIVE_SIMULATOR
        (void)anim;
        lv_screen_load(target);
#else
        lv_screen_load_anim(target, anim, 250, 0, false);
#endif
    }
    ui_platform_unlock();
}

static float decode_be_float(const uint8_t* data)
{
    uint32_t raw = 0;
    std::memcpy(&raw, data + 4, sizeof(raw));
    raw = __builtin_bswap32(raw);
    float value = 0.0f;
    std::memcpy(&value, &raw, sizeof(value));
    return value;
}

static double decode_be_double_l(const uint8_t* data)
{
    uint32_t raw = 0;
    std::memcpy(&raw, data + 4, sizeof(raw));
    raw = __builtin_bswap32(raw);
    return static_cast<int32_t>(raw) / 1E7;
}

static uint16_t decode_be_u16(const uint8_t* data)
{
    uint16_t raw = 0;
    std::memcpy(&raw, data + 4, sizeof(raw));
    return __builtin_bswap16(raw);
}

static int decode_char(const uint8_t* data)
{
    return static_cast<int>(data[4]);
}

static int open_can_socket(const std::string& iface)
{
    const int fd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (fd < 0)
    {
        std::fprintf(stderr, "Failed to create CAN socket for %s: %s\n", iface.c_str(), std::strerror(errno));
        return -1;
    }

    ifreq ifr {};
    std::snprintf(ifr.ifr_name, IFNAMSIZ, "%s", iface.c_str());
    if (ioctl(fd, SIOCGIFINDEX, &ifr) < 0)
    {
        std::fprintf(stderr, "Failed to resolve CAN interface %s: %s\n", iface.c_str(), std::strerror(errno));
        close(fd);
        return -1;
    }

    sockaddr_can addr {};
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;

    timeval timeout {};
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
    {
        std::fprintf(stderr, "Failed to bind %s: %s\n", iface.c_str(), std::strerror(errno));
        close(fd);
        return -1;
    }

    return fd;
}

static void can_receiver_task(std::string iface)
{
    const int fd = open_can_socket(iface);
    if (fd < 0)
    {
        g_running = false;
        return;
    }

    while (g_running.load())
    {
        can_frame frame {};
        const ssize_t nread = read(fd, &frame, sizeof(frame));
        if (nread < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
            std::fprintf(stderr, "CAN read error on %s: %s\n", iface.c_str(), std::strerror(errno));
            break;
        }
        if (nread != sizeof(frame)) continue;
        if (frame.can_id & CAN_EFF_FLAG) continue;

        std::lock_guard lock(g_flight_state.mtx);
        switch (frame.can_id)
        {
        case 315:
            g_flight_state.ias = decode_be_float(frame.data);
            g_flight_state.ias_mps = g_flight_state.ias;
            g_flight_state.last_relevant_rx_ms = FlightData::monotonic_ms();
            break;
        case 316: g_flight_state.tas = decode_be_float(frame.data);
            break;
        case 321: g_flight_state.heading = decode_be_float(frame.data);
            break;
        case 322: g_flight_state.alt = decode_be_float(frame.data);
            break;
        case 333: g_flight_state.wind_speed = decode_be_float(frame.data);
            break;
        case 334: g_flight_state.wind_direction = decode_be_float(frame.data);
            break;
        case 340:
            g_flight_state.flap = decode_char(frame.data);
            g_flight_state.last_relevant_rx_ms = FlightData::monotonic_ms();
            break;
        case 354: g_flight_state.vario = decode_be_float(frame.data);
            break;
        case 1036: g_flight_state.lat = decode_be_double_l(frame.data);
            break;
        case 1037: g_flight_state.lon = decode_be_double_l(frame.data);
            break;
        case 1039:
            g_flight_state.gs = decode_be_float(frame.data);
            g_flight_state.last_relevant_rx_ms = FlightData::monotonic_ms();
            break;
        case 1040: g_flight_state.tt = decode_be_float(frame.data);
            break;
        case 1515:
            g_flight_state.dry_and_ballast_mass = decode_be_u16(frame.data);
            break;
        case 1506: g_flight_state.enl = decode_be_u16(frame.data);
            break;
        default:
            break;
        }
    }

    close(fd);
    g_running = false;
}

static void pump_ui_for(std::chrono::milliseconds duration)
{
    const auto until = std::chrono::steady_clock::now() + duration;
    while (g_running.load() && std::chrono::steady_clock::now() < until)
    {
        const uint32_t sleep_ms = lv_timer_handler();
        std::this_thread::sleep_for(std::chrono::milliseconds(std::clamp<uint32_t>(sleep_ms, 1, 10)));
    }
}

float get_ias_kmh()
{
    std::lock_guard lock(g_flight_state.mtx);
    return g_flight_state.ias * 3.6f;
}

float get_weight_kg()
{
    std::lock_guard lock(g_flight_state.mtx);
    return g_flight_state.dry_and_ballast_mass / 10.0f + 84.0f;
}

flaputils::FlapSymbolResult get_flap_actual()
{
    std::lock_guard lock(g_flight_state.mtx);
    return flaputils::get_flap_symbol(g_flight_state.flap);
}

flaputils::FlapSymbolResult get_flap_target()
{
    std::lock_guard lock(g_flight_state.mtx);
    const float weight_kg = g_flight_state.dry_and_ballast_mass / 10.0f + 84.0f;
    return flaputils::get_optimal_flap(weight_kg, g_flight_state.ias * 3.6f);
}

bool is_stale()
{
    return g_flight_state.is_stale();
}

static void print_task(FlightData* data)
{
    while (g_running.load())
    {
        data->print();
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

int main(int argc, char** argv)
{
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    const SimulatorConfig cfg = parse_args(argc, argv);
    if (!flaputils::load_data("spiffs_data/flapDescriptor.json"))
    {
        std::fprintf(stderr, "Failed to load spiffs_data/flapDescriptor.json\n");
        return 1;
    }

    std::thread can_thread(can_receiver_task, cfg.can_iface);
    std::thread print_thread(print_task, &g_flight_state);

    ui_init();
    set_label1(APP_NAME);
    set_label2("Version: " APP_VERSION);
    set_label3("Git Rev: " GIT_REVISION);

    if (cfg.splash_ms > 0)
    {
        pump_ui_for(std::chrono::milliseconds(cfg.splash_ms));
    }

    load_screen(cfg.start_screen, LV_SCR_LOAD_ANIM_FADE_ON);

    auto next_cycle = std::chrono::steady_clock::now() + std::chrono::seconds(cfg.cycle_seconds);
    int current_screen = cfg.start_screen;

    while (g_running.load())
    {
        const uint32_t sleep_ms = lv_timer_handler();

        if (cfg.auto_cycle && std::chrono::steady_clock::now() >= next_cycle)
        {
            current_screen = (current_screen % 4) + 1;
            load_screen(current_screen, LV_SCR_LOAD_ANIM_FADE_ON);
            next_cycle = std::chrono::steady_clock::now() + std::chrono::seconds(cfg.cycle_seconds);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(std::clamp<uint32_t>(sleep_ms, 1, 10)));
    }

    can_thread.join();
    print_thread.join();
    return 0;
}
#endif // NATIVE_TEST_BUILD
