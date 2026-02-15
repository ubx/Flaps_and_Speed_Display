#include <cmath>
#ifndef NATIVE_BUILD

#include "lvgl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/spi_master.h"
#include "driver/gpio.h"

#include "esp_heap_caps.h"
#include "esp_log.h"

#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_co5300.h"   // CO5300 driver (Waveshare AMOLED 1.75)

static const char *TAG = "display";

/* Waveshare ESP32-S3-Touch-AMOLED-1.75 */
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

static lv_display_t *s_disp = nullptr;
static esp_lcd_panel_handle_t s_panel_handle = nullptr;

/* ---------------- Flush ready callback (IDF compatible) ---------------- */
static bool notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t /*panel_io*/,
                                    esp_lcd_panel_io_event_data_t * /*edata*/,
                                    void * /*user_ctx*/)
{
    if (s_disp) {
        lv_display_flush_ready(s_disp);
    }
    return false;
}

/* ---------------- LVGL flush ---------------- */
static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    // Comment out once working (otherwise log spam)
    // ESP_LOGI(TAG, "FLUSH");

    esp_lcd_panel_handle_t panel =
        (esp_lcd_panel_handle_t)lv_display_get_user_data(disp);

    esp_lcd_panel_draw_bitmap(panel,
                              area->x1, area->y1,
                              area->x2 + 1, area->y2 + 1,
                              px_map);
}

/* ---------------- Simple test screen ---------------- */
static void create_test_screen()
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_text(label, "AMOLED OK");
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_center(label);
}

/* ---------------- LVGL task ---------------- */
static void lvgl_task(void *)
{
    const uint32_t period_ms = 10;
    while (true) {
        lv_tick_inc(period_ms);
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(period_ms));
    }
}

void display_start()
{
    ESP_LOGI(TAG, "Power on AMOLED");

    /* ---------- POWER ENABLE ---------- */
    gpio_config_t pwr_gpio_config = {};
    pwr_gpio_config.pin_bit_mask = 1ULL << LCD_PIN_NUM_LCD_PWR;
    pwr_gpio_config.mode = GPIO_MODE_OUTPUT;
    pwr_gpio_config.pull_up_en = GPIO_PULLUP_DISABLE;
    pwr_gpio_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    pwr_gpio_config.intr_type = GPIO_INTR_DISABLE;
    ESP_ERROR_CHECK(gpio_config(&pwr_gpio_config));
    ESP_ERROR_CHECK(gpio_set_level((gpio_num_t)LCD_PIN_NUM_LCD_PWR, 1));
    vTaskDelay(pdMS_TO_TICKS(300)); // important for AMOLED rail

    /* ---------- HARD RESET ---------- */
    gpio_config_t rst_gpio_config = {};
    rst_gpio_config.pin_bit_mask = 1ULL << LCD_PIN_NUM_LCD_RST;
    rst_gpio_config.mode = GPIO_MODE_OUTPUT;
    rst_gpio_config.pull_up_en = GPIO_PULLUP_DISABLE;
    rst_gpio_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    rst_gpio_config.intr_type = GPIO_INTR_DISABLE;
    ESP_ERROR_CHECK(gpio_config(&rst_gpio_config));

    gpio_set_level((gpio_num_t)LCD_PIN_NUM_LCD_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level((gpio_num_t)LCD_PIN_NUM_LCD_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level((gpio_num_t)LCD_PIN_NUM_LCD_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(150));

    /* ---------- QSPI BUS (NO MACROS, C++ SAFE) ---------- */
    ESP_LOGI(TAG, "Init QSPI bus");

    spi_bus_config_t buscfg = {};
    buscfg.sclk_io_num  = LCD_PIN_NUM_QSPI_PCLK;

    // QSPI data lines
    buscfg.data0_io_num = LCD_PIN_NUM_QSPI_D0;
    buscfg.data1_io_num = LCD_PIN_NUM_QSPI_D1;
    buscfg.data2_io_num = LCD_PIN_NUM_QSPI_D2;
    buscfg.data3_io_num = LCD_PIN_NUM_QSPI_D3;

    // Not used in QSPI mode
    buscfg.mosi_io_num = -1;
    buscfg.miso_io_num = -1;
    buscfg.quadwp_io_num = -1;
    buscfg.quadhd_io_num = -1;

    // Enough for partial updates
    buscfg.max_transfer_sz = LCD_H_RES * 80 * sizeof(uint16_t);

    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));

    /* ---------- PANEL IO (QSPI, NO MACROS, C++ SAFE) ---------- */
    ESP_LOGI(TAG, "Install panel IO");

    esp_lcd_panel_io_handle_t io_handle = nullptr;

    esp_lcd_panel_io_spi_config_t io_config = {};
    io_config.cs_gpio_num = LCD_PIN_NUM_QSPI_CS;
    io_config.dc_gpio_num = -1; // QSPI command/param handled by driver
    io_config.spi_mode = 0;
    io_config.pclk_hz = 20 * 1000 * 1000;   // safe start
    io_config.trans_queue_depth = 10;
    io_config.on_color_trans_done = notify_lvgl_flush_ready;
    io_config.user_ctx = nullptr;
    io_config.lcd_cmd_bits = 8;
    io_config.lcd_param_bits = 8;
    io_config.flags.quad_mode = 1;          // ⭐ important

    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(
        (esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_config, &io_handle));

    /* ---------- CO5300 PANEL ---------- */
    ESP_LOGI(TAG, "Install CO5300 panel");

    co5300_vendor_config_t vendor_config = {};
    vendor_config.flags.use_qspi_interface = 1;

    esp_lcd_panel_dev_config_t panel_config = {};
    panel_config.reset_gpio_num = LCD_PIN_NUM_LCD_RST;
    panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    panel_config.data_endian = LCD_RGB_DATA_ENDIAN_BIG;
    panel_config.bits_per_pixel = 16;
    panel_config.vendor_config = &vendor_config;

    ESP_ERROR_CHECK(esp_lcd_new_panel_co5300(io_handle, &panel_config, &s_panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel_handle));

    // These two are commonly needed depending on panel batch.
    // Try (true/false) combos if you see mirrored/inverted later.
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(s_panel_handle, true));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(s_panel_handle, false, false));

    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel_handle, true));

    /* ---------- LVGL ---------- */
    ESP_LOGI(TAG, "Init LVGL");
    lv_init();

    s_disp = lv_display_create(LCD_H_RES, LCD_V_RES);
    lv_display_set_default(s_disp);
    lv_display_set_user_data(s_disp, s_panel_handle);

    // Small DMA buffer so it works even without PSRAM
    size_t buf_size = LCD_H_RES * 40 * sizeof(lv_color_t);
    lv_color_t *buf = (lv_color_t *)heap_caps_malloc(buf_size, MALLOC_CAP_DMA);
    assert(buf);

    lv_display_set_buffers(s_disp, buf, nullptr, buf_size, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(s_disp, lvgl_flush_cb);

    create_test_screen();

    xTaskCreatePinnedToCore(lvgl_task, "lvgl", 8192, nullptr, 5, nullptr, tskNO_AFFINITY);

    ESP_LOGI(TAG, "Display started");

    esp_lcd_panel_invert_color(s_panel_handle, false);
    esp_lcd_panel_mirror(s_panel_handle, true, false);

}

#else
void display_start() {}
#endif
