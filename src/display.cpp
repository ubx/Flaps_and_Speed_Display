#include <cmath>
#ifndef NATIVE_BUILD
#include "lvgl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_sh8601.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

// Forward declared in main.cpp
float get_ias_kmh();

static const char *TAG = "display";

// #define LCD_H_RES              390
// #define LCD_V_RES              450

#define LCD_H_RES 466
#define LCD_V_RES 466

#define LCD_PIN_NUM_QSPI_PCLK  6
#define LCD_PIN_NUM_QSPI_CS    7
#define LCD_PIN_NUM_QSPI_D0    8
#define LCD_PIN_NUM_QSPI_D1    9
#define LCD_PIN_NUM_QSPI_D2    10
#define LCD_PIN_NUM_QSPI_D3    11
#define LCD_PIN_NUM_LCD_RST    12
#define LCD_PIN_NUM_LCD_PWR    15

static lv_obj_t* s_scale = nullptr;
static lv_obj_t* s_needle = nullptr;
static lv_obj_t* s_value_label = nullptr;
static lv_display_t* s_disp = nullptr;
static esp_lcd_panel_handle_t s_panel_handle = nullptr;
static esp_lcd_panel_io_handle_t s_io_handle = nullptr;

static const sh8601_lcd_init_cmd_t custom_init_cmds[] = {
    {0x11, nullptr, 0, 120}, // Sleep Out
    {0x44, (uint8_t[]){0x00, 0xc8}, 2, 0},
    {0x35, (uint8_t[]){0x00}, 1, 0},
    {0x53, (uint8_t[]){0x20}, 1, 10},
    {0x29, nullptr, 0, 20}, // Display On
};

static bool notify_lvgl_flush_ready(
    esp_lcd_panel_io_handle_t panel_io,
    esp_lcd_panel_io_event_data_t *edata,
    void *user_ctx)
{
    if (s_disp) {
        lv_display_flush_ready(s_disp);
    }
    return false;
}


static void example_lvgl_flush_cb(lv_display_t * disp,
                                  const lv_area_t * area,
                                  uint8_t * px_map)
{
    esp_lcd_panel_handle_t panel_handle =
        (esp_lcd_panel_handle_t)lv_display_get_user_data(disp);

    esp_lcd_panel_draw_bitmap(panel_handle,
                              area->x1,
                              area->y1,
                              area->x2 + 1,
                              area->y2 + 1,
                              px_map);
}

static void create_speed_gauge()
{
    lv_obj_t* scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);

    s_scale = lv_scale_create(scr);
    lv_obj_center(s_scale);
    lv_obj_set_size(s_scale, 320, 320);

    lv_scale_set_mode(s_scale, LV_SCALE_MODE_ROUND_INNER);
    lv_scale_set_range(s_scale, 40, 280);
    lv_scale_set_total_tick_count(s_scale, 25);
    lv_scale_set_major_tick_every(s_scale, 5);
    lv_scale_set_angle_range(s_scale, 280);
    lv_scale_set_rotation(s_scale, 130);
    lv_scale_set_label_show(s_scale, true);

    lv_obj_set_style_length(s_scale, 8, LV_PART_ITEMS);
    lv_obj_set_style_length(s_scale, 14, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(s_scale, 16, LV_PART_MAIN);

    s_needle = lv_line_create(s_scale);
    lv_obj_set_style_line_color(s_needle, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_line_width(s_needle, 10, LV_PART_MAIN);
    lv_obj_set_style_line_rounded(s_needle, true, LV_PART_MAIN);
    lv_obj_set_style_pad_right(s_needle, 46, LV_PART_MAIN);

    lv_obj_t* center = lv_obj_create(scr);
    lv_obj_set_size(center, 140, 140);
    lv_obj_center(center);
    lv_obj_set_style_radius(center, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(center, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(center, 0, 0);
    lv_obj_set_style_pad_all(center, 8, 0);

    s_value_label = lv_label_create(center);
    lv_obj_center(s_value_label);
    lv_obj_set_style_text_font(s_value_label, &lv_font_montserrat_26, 0);
    lv_label_set_text(s_value_label, "0");

    lv_obj_t* unit = lv_label_create(center);
    lv_obj_set_style_text_font(unit, &lv_font_montserrat_14, 0);
    lv_label_set_text(unit, "km/h");
    lv_obj_align(unit, LV_ALIGN_BOTTOM_MID, 0, -15);

    lv_scale_set_line_needle_value(s_scale, s_needle, 22, 40);
}

static void lvgl_task(void* /*arg*/)
{
    const uint32_t period_ms = 10;
    uint32_t accum = 0;

    while (true)
    {
        lv_tick_inc(period_ms);
        lv_timer_handler();

        accum += period_ms;
        if (accum >= 100) {
            accum = 0;
            float v = get_ias_kmh();
            if (std::isnan(v) || std::isinf(v)) v = 0.0f;
            if (v < 40.0f) v = 40.0f;
            if (v > 280.0f) v = 280.0f;

            if (s_scale && s_needle) {
                lv_scale_set_line_needle_value(
                    s_scale, s_needle, 22, (int32_t)v);
            }
            if (s_value_label) {
                lv_label_set_text_fmt(
                    s_value_label, "%d", (int)(v + 0.5f));
            }
        }
        vTaskDelay(pdMS_TO_TICKS(period_ms));
    }
}

void display_start()
{
    static bool inited = false;
    if (inited) return;
    inited = true;

    ESP_LOGI(TAG, "Initialize LCD Power");

    gpio_config_t pwr_gpio_config = {
        .pin_bit_mask = 1ULL << LCD_PIN_NUM_LCD_PWR,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_ERROR_CHECK(gpio_config(&pwr_gpio_config));
    ESP_ERROR_CHECK(gpio_set_level((gpio_num_t)LCD_PIN_NUM_LCD_PWR, 1));
    vTaskDelay(pdMS_TO_TICKS(100));

    ESP_LOGI(TAG, "Initialize SPI bus");

    const spi_bus_config_t buscfg =
        SH8601_PANEL_BUS_QSPI_CONFIG(
            LCD_PIN_NUM_QSPI_PCLK,
            LCD_PIN_NUM_QSPI_D0,
            LCD_PIN_NUM_QSPI_D1,
            LCD_PIN_NUM_QSPI_D2,
            LCD_PIN_NUM_QSPI_D3,
            65535);

    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));

    ESP_LOGI(TAG, "Install panel IO");

    const esp_lcd_panel_io_spi_config_t io_config =
        SH8601_PANEL_IO_QSPI_CONFIG(
            LCD_PIN_NUM_QSPI_CS,
            notify_lvgl_flush_ready,
            NULL); // ⚠️ set later

    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(
        (esp_lcd_spi_bus_handle_t)SPI2_HOST,
        &io_config,
        &s_io_handle));

    ESP_LOGI(TAG, "Install SH8601 panel driver");

    const sh8601_vendor_config_t vendor_config = {
        .init_cmds = custom_init_cmds,
        .init_cmds_size =
            sizeof(custom_init_cmds) / sizeof(custom_init_cmds[0]),
        .flags = {.use_qspi_interface = 1},
    };

    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = LCD_PIN_NUM_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .data_endian = LCD_RGB_DATA_ENDIAN_BIG,
        .bits_per_pixel = 16,
        .flags = {},
        .vendor_config = (void*)&vendor_config,
    };

    ESP_ERROR_CHECK(esp_lcd_new_panel_sh8601(
        s_io_handle, &panel_config, &s_panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel_handle, true));

    // ---------------- LVGL ----------------

    lv_init();

    s_disp = lv_display_create(LCD_H_RES, LCD_V_RES);
    lv_display_set_default(s_disp);                 // ⭐ CRITICAL
    lv_display_set_user_data(s_disp, s_panel_handle);

    // now connect flush ready to correct display

    size_t buf_size = LCD_H_RES * 40 * sizeof(lv_color_t);
    lv_color_t *buf1 =
        (lv_color_t *)heap_caps_malloc(buf_size, MALLOC_CAP_DMA);
    assert(buf1);

    lv_display_set_buffers(
        s_disp, buf1, NULL, buf_size,
        LV_DISPLAY_RENDER_MODE_PARTIAL);

    lv_display_set_flush_cb(s_disp, example_lvgl_flush_cb);

    create_speed_gauge();

    xTaskCreatePinnedToCore(
        lvgl_task, "lvgl", 8192, NULL, 5, NULL, tskNO_AFFINITY);
}

#else
void display_start() {}
#endif
