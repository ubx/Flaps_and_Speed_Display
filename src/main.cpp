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
#include <endian.h>

static const char *TAG = "CANReceiver";

struct FlightData {
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
    uint16_t pilot_mass = 0;
    uint16_t enl = 0;

    void update_float(const std::string& key, float value) {
        std::lock_guard<std::mutex> lock(mtx);
        if (key == "ias") ias = value;
        else if (key == "tas") tas = value;
        else if (key == "cas") cas = value;
        else if (key == "alt") alt = value;
        else if (key == "vario") vario = value;
        else if (key == "gs") gs = value;
        else if (key == "tt") tt = value;
    }

    void update_double(const std::string& key, double value) {
        std::lock_guard<std::mutex> lock(mtx);
        if (key == "lat") lat = value;
        else if (key == "lon") lon = value;
    }

    void update_int(const std::string& key, int value) {
        std::lock_guard<std::mutex> lock(mtx);
        if (key == "flap") flap = value;
    }

    void update_uint16(const std::string& key, uint16_t value) {
        std::lock_guard<std::mutex> lock(mtx);
        if (key == "pilot_mass") pilot_mass = value;
        else if (key == "enl") enl = value;
    }

    void print() {
        std::lock_guard<std::mutex> lock(mtx);
        printf("FlightData: IAS=%.2f, TAS=%.2f, CAS=%.2f, ALT=%.2f, Vario=%.2f, Flap=%d, Lat=%.7f, Lon=%.7f, GS=%.2f, TT=%.2f, PilotMass=%u, ENL=%u\n",
               ias, tas, cas, alt, vario, flap, lat, lon, gs, tt, pilot_mass, enl);
    }
};

class CANReceiver {
public:
    CANReceiver(FlightData& flight_data) : flight_data(flight_data) {}

    void start() {
        xTaskCreatePinnedToCore(receive_task, "can_rx_task", 4096, this, 5, NULL, tskNO_AFFINITY);
    }

private:
    FlightData& flight_data;

    static void receive_task(void *arg) {
        CANReceiver *self = (CANReceiver *)arg;
        self->run();
    }

    void run() {
        twai_message_t message;
        while (true) {
            if (twai_receive(&message, pdMS_TO_TICKS(1000)) == ESP_OK) {
                handle_message(message);
            }
        }
    }

    void handle_message(const twai_message_t& msg) {
        if (!(msg.flags & TWAI_MSG_FLAG_EXTD)) { // Standard Frame
            uint32_t id = msg.identifier;
            switch (id) {
                case 315: flight_data.update_float("ias", get_float(msg.data)); break;
                case 316: flight_data.update_float("tas", get_float(msg.data)); break;
                case 317: flight_data.update_float("cas", get_float(msg.data)); break;
                case 322: flight_data.update_float("alt", get_float(msg.data)); break;
                case 340: flight_data.update_int("flap", get_char(msg.data)); break;
                case 354: flight_data.update_float("vario", get_float(msg.data)); break;
                case 1036: flight_data.update_double("lat", get_double_l(msg.data)); break;
                case 1037: flight_data.update_double("lon", get_double_l(msg.data)); break;
                case 1039: flight_data.update_float("gs", get_float(msg.data)); break;
                case 1040: flight_data.update_float("tt", get_float(msg.data)); break;
                case 1316: flight_data.update_uint16("pilot_mass", get_ushort(msg.data)); break;
                case 1506: flight_data.update_uint16("enl", get_ushort(msg.data)); break;
                default:
                    // Unknown ID
                    break;
            }
        }
    }

    float get_float(const uint8_t* data) {
        uint32_t val;
        memcpy(&val, &data[4], 4);
        val = be32toh(val);
        float f;
        memcpy(&f, &val, 4);
        return f;
    }

    double get_double_l(const uint8_t* data) {
        uint32_t val;
        memcpy(&val, &data[4], 4);
        val = be32toh(val);
        int32_t l = (int32_t)val;
        return l / 1E7;
    }

    uint16_t get_ushort(const uint8_t* data) {
        uint16_t val;
        memcpy(&val, &data[4], 2);
        return be16toh(val);
    }

    int get_char(const uint8_t* data) {
        return (int)data[4];
    }
};

static FlightData flight_state;
static CANReceiver receiver(flight_state);

void print_task(void *arg) {
    FlightData *data = (FlightData *)arg;
    while (true) {
        data->print();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

extern "C" void app_main(void)
{
    // Initialize TWAI driver
    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT((gpio_num_t)0, (gpio_num_t)1, TWAI_MODE_NORMAL);
    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    if (twai_driver_install(&g_config, &t_config, &f_config) == ESP_OK) {
        if (twai_start() == ESP_OK) {
            ESP_LOGI(TAG, "TWAI Driver started");
            receiver.start();
            xTaskCreate(print_task, "print_task", 4096, &flight_state, 2, NULL);
        } else {
            ESP_LOGE(TAG, "Failed to start TWAI driver");
        }
    } else {
        ESP_LOGE(TAG, "Failed to install TWAI driver");
    }
}
