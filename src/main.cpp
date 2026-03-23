#include "flight_data.hpp"
#include "can_decoder.hpp"
#include "flaputils.hpp"
#include "ui/ui.h"
#include "ui/ui_helpers.hpp"
#include "ui/screens/screen2.hpp"

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
#include "ble_ota.hpp"
#include "bsp/esp32_s3_touch_amoled_1_75.h"
#include "lvgl.h"

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
#include "lvgl.h"
#include "platform/ui_platform.hpp"
#include "ui/screens/screen1.hpp"
#include "ui/screens/screen3.hpp"
#include "ui/screens/screen4.hpp"
#endif

#define APP_NAME "Flaps & Speed"
#define APP_VERSION "1.0.0"

#ifndef GIT_REVISION
#define GIT_REVISION "unknown"
#endif

static const char* TAG = "main";
static FlightData g_flight_state;

#ifndef NATIVE_TEST_BUILD

class CANReceiver
{
public:
    CANReceiver(FlightData& flight_data) : flight_data(flight_data) {}

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
            switch (msg.identifier)
            {
            case 315: flight_data.update_float("ias", CANDecoder::decode_float(msg.data)); break;
            case 316: flight_data.update_float("tas", CANDecoder::decode_float(msg.data)); break;
            case 321: flight_data.update_float("heading", CANDecoder::decode_float(msg.data)); break;
            case 322: flight_data.update_float("alt", CANDecoder::decode_float(msg.data)); break;
            case 1519: flight_data.update_float("alt_corr", CANDecoder::decode_float(msg.data)); break;
            case 333: flight_data.update_float("wind_speed", CANDecoder::decode_float(msg.data)); break;
            case 334: flight_data.update_float("wind_direction", CANDecoder::decode_float(msg.data)); break;
            case 340: flight_data.update_int("flap", CANDecoder::decode_char(msg.data)); break;
            case 1039: flight_data.update_float("gps_ground_speed", CANDecoder::decode_float(msg.data)); break;
            case 1040: flight_data.update_float("gps_true_track", CANDecoder::decode_float(msg.data)); break;
            case 1515: flight_data.update_uint16("dry_and_ballast_mass", CANDecoder::decode_u16(msg.data)); break;
            case 1506: flight_data.update_uint16("enl", CANDecoder::decode_u16(msg.data)); break;
            default: break;
            }
        }
    }
};

static CANReceiver receiver(g_flight_state);

static void configure_task_wdt_for_ui(void)
{
#if CONFIG_ESP_TASK_WDT_EN && !CONFIG_FREERTOS_UNICORE
    esp_task_wdt_config_t twdt_cfg = {
        .timeout_ms = CONFIG_ESP_TASK_WDT_TIMEOUT_S * 1000,
        .idle_core_mask = (1U << 0),
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

[[noreturn]] void print_task(void* arg)
{
    auto data = static_cast<FlightData*>(arg);
    while (true)
    {
#ifndef ENABLE_DIAGNOSTICS
        print_flight_data(*data);
#endif
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

extern "C" void app_main(void)
{
    configure_task_wdt_for_ui();
    vTaskDelay(pdMS_TO_TICKS(2000));

    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = "spiffs",
        .max_files = 5,
        .format_if_mount_failed = false
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret == ESP_OK)
    {
        if (flaputils::load_data("/spiffs/flapDescriptor.json"))
            ESP_LOGI(TAG, "Flap data loaded successfully");
        else
            ESP_LOGE(TAG, "Failed to load flap data from SPIFFS");
    }

    ble_ota_init();

    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(static_cast<gpio_num_t>(TWAI_TX_GPIO),
        static_cast<gpio_num_t>(TWAI_RX_GPIO), TWAI_MODE_NORMAL);
    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    if (twai_driver_install(&g_config, &t_config, &f_config) == ESP_OK && twai_start() == ESP_OK)
    {
        receiver.start();
        xTaskCreate(print_task, "print_task", 4096, &g_flight_state, 2, nullptr);
        ui_init();
        set_label1(APP_NAME);
        set_label2("Version: " APP_VERSION);
        char lvgl_ver[32];
        std::snprintf(lvgl_ver, sizeof(lvgl_ver), "LVGL v%d.%d.%d", LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);
        set_label3(lvgl_ver);
        set_label4("Git Rev: " GIT_REVISION);
        vTaskDelay(pdMS_TO_TICKS(5000));

        if (bsp_display_lock(-1) == ESP_OK)
        {
            lv_screen_load_anim(screen2_get(), LV_SCR_LOAD_ANIM_FADE_ON, 500, 0, false);
            bsp_display_unlock();
        }
    }
}

#else // NATIVE_TEST_BUILD

struct SimulatorConfig
{
    int start_screen = 2;
    bool auto_cycle = false;
    int cycle_seconds = 8;
    int splash_ms = 1500;
    std::string can_iface = "can0";
};

static std::atomic<bool> g_running{true};
static void handle_signal(int) { g_running = false; }

static int parse_int_arg(const char* value, int fallback) { return value ? std::atoi(value) : fallback; }

static SimulatorConfig parse_args(int argc, char** argv)
{
    SimulatorConfig cfg;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--screen") == 0 && i + 1 < argc)
            cfg.start_screen = std::clamp(parse_int_arg(argv[++i], cfg.start_screen), 1, 4);
        else if (std::strcmp(argv[i], "--auto-cycle") == 0)
            cfg.auto_cycle = true;
        else if (std::strcmp(argv[i], "--cycle-seconds") == 0 && i + 1 < argc)
            cfg.cycle_seconds = std::max(1, parse_int_arg(argv[++i], cfg.cycle_seconds));
        else if (std::strcmp(argv[i], "--can-iface") == 0 && i + 1 < argc)
            cfg.can_iface = argv[++i];
        else if (std::strcmp(argv[i], "--no-splash") == 0)
            cfg.splash_ms = 0;
        else if (std::strcmp(argv[i], "--help") == 0)
        {
            std::printf("Usage: %s [--screen 1..4] [--auto-cycle] [--cycle-seconds N] [--can-iface can0] [--no-splash]\n", argv[0]);
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

static int open_can_socket(const std::string& iface)
{
    const int fd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (fd < 0) return -1;

    ifreq ifr {};
    std::snprintf(ifr.ifr_name, IFNAMSIZ, "%s", iface.c_str());
    if (ioctl(fd, SIOCGIFINDEX, &ifr) < 0) { close(fd); return -1; }

    sockaddr_can addr {};
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;

    timeval timeout { .tv_sec = 1, .tv_usec = 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) { close(fd); return -1; }
    return fd;
}

static void can_receiver_task(std::string iface)
{
    const int fd = open_can_socket(iface);
    if (fd < 0) { g_running = false; return; }

    while (g_running.load())
    {
        can_frame frame {};
        const ssize_t nread = read(fd, &frame, sizeof(frame));
        if (nread < 0) { if (errno == EAGAIN || errno == EWOULDBLOCK) continue; break; }
        if (nread != sizeof(frame) || (frame.can_id & CAN_EFF_FLAG)) continue;

        std::lock_guard lock(g_flight_state.mtx);
        switch (frame.can_id)
        {
        case 315:
            g_flight_state.ias = CANDecoder::decode_float(frame.data);
            g_flight_state.last_relevant_rx_ms = FlightData::monotonic_ms();
            break;
        case 316: g_flight_state.tas = CANDecoder::decode_float(frame.data); break;
        case 321: g_flight_state.heading = CANDecoder::decode_float(frame.data); break;
        case 322: g_flight_state.alt = CANDecoder::decode_float(frame.data); break;
        case 1519: g_flight_state.alt_corr = CANDecoder::decode_float(frame.data); break;
        case 333: g_flight_state.wind_speed = CANDecoder::decode_float(frame.data); break;
        case 334: g_flight_state.wind_direction = CANDecoder::decode_float(frame.data); break;
        case 340:
            g_flight_state.flap = CANDecoder::decode_char(frame.data);
            g_flight_state.last_relevant_rx_ms = FlightData::monotonic_ms();
            break;
        case 354: g_flight_state.vario = CANDecoder::decode_float(frame.data); break;
        case 1039:
            g_flight_state.gps_ground_speed = CANDecoder::decode_float(frame.data);
            g_flight_state.last_relevant_rx_ms = FlightData::monotonic_ms();
            break;
        case 1040: g_flight_state.gps_true_track = CANDecoder::decode_float(frame.data); break;
        case 1515: g_flight_state.dry_and_ballast_mass = CANDecoder::decode_u16(frame.data); break;
        case 1506: g_flight_state.enl = CANDecoder::decode_u16(frame.data); break;
        default: break;
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

static void print_task_native(FlightData* data)
{
    while (g_running.load())
    {
        print_flight_data(*data);
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

int main(int argc, char** argv)
{
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);
    const SimulatorConfig cfg = parse_args(argc, argv);

    if (!flaputils::load_data("spiffs_data/flapDescriptor.json")) return 1;

    std::thread can_thread(can_receiver_task, cfg.can_iface);
    std::thread print_thread(print_task_native, &g_flight_state);

    ui_init();
    set_label1(APP_NAME);
    set_label2("Version: " APP_VERSION);
    char lvgl_ver[32];
    std::snprintf(lvgl_ver, sizeof(lvgl_ver), "LVGL v%d.%d.%d", LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);
    set_label3(lvgl_ver);
    set_label4("Git Rev: " GIT_REVISION);

    if (cfg.splash_ms > 0) pump_ui_for(std::chrono::milliseconds(cfg.splash_ms));
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

float get_ias_kmh() { return ::get_ias_kmh(g_flight_state); }
float get_weight_kg() { return ::get_weight_kg(g_flight_state); }
float get_alt_m() { return ::get_alt_m(g_flight_state); }
float get_heading() { return ::get_heading(g_flight_state); }
float get_wind_speed_kmh() { return ::get_wind_speed_kmh(g_flight_state); }
float get_wind_direction() { return ::get_wind_direction(g_flight_state); }
float get_gps_ground_speed_kmh() { return ::get_gps_ground_speed_kmh(g_flight_state); }
float get_gps_true_track() { return ::get_gps_true_track(g_flight_state); }
flaputils::FlapSymbolResult get_flap_actual() { return ::get_flap_actual(g_flight_state); }
flaputils::FlapSymbolResult get_flap_target() { return ::get_flap_target(g_flight_state); }
bool is_stale() { return g_flight_state.is_stale(); }
