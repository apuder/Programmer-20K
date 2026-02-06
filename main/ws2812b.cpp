#include "ws2812b.h"
#include "driver/rmt_tx.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include <cstring>

static const char *TAG = "WS2812B";

// WS2812B timing: 800kHz data rate
// 0 bit: 0.4µs high + 0.85µs low = 1.25µs total
// 1 bit: 0.8µs high + 0.45µs low = 1.25µs total

#define WS2812B_GPIO 25
#define WS2812B_NUM_LEDS 1
#define WS2812B_RMT_CLK_KHZ 8000  // 8MHz clock = 125ns resolution

// Buffer to hold RGB data
static uint8_t led_buffer[WS2812B_NUM_LEDS * 3]; // RGB bytes
static rmt_channel_handle_t rmt_channel = NULL;
static rmt_encoder_handle_t rmt_encoder = NULL;

void ws2812b_init()
{
    ESP_LOGI(TAG, "Initializing WS2812B on GPIO %d using RMT", WS2812B_GPIO);

    // Create RMT TX channel
    rmt_tx_channel_config_t tx_chan_config = {
        .gpio_num = (gpio_num_t) WS2812B_GPIO,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = WS2812B_RMT_CLK_KHZ * 1000,  // 8MHz clock
        .mem_block_symbols = 64,
        .trans_queue_depth = 4,
        .flags = {
            .invert_out = false,
            .with_dma = false,
        }
    };

    esp_err_t ret = rmt_new_tx_channel(&tx_chan_config, &rmt_channel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "RMT TX channel creation failed: %s", esp_err_to_name(ret));
        return;
    }

    // Create the WS2812B bytes encoder
    rmt_bytes_encoder_config_t bytes_config = {};
    bytes_config.bit0.level0 = 1;
    bytes_config.bit0.duration0 = 3;
    bytes_config.bit0.level1 = 0;
    bytes_config.bit0.duration1 = 7;
    bytes_config.bit1.level0 = 1;
    bytes_config.bit1.duration0 = 6;
    bytes_config.bit1.level1 = 0;
    bytes_config.bit1.duration1 = 5;
    bytes_config.flags.msb_first = 1;

    ret = rmt_new_bytes_encoder(&bytes_config, &rmt_encoder);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "RMT encoder creation failed: %s", esp_err_to_name(ret));
        return;
    }

    // Enable the RMT channel
    ret = rmt_enable(rmt_channel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "RMT enable failed: %s", esp_err_to_name(ret));
        return;
    }

    // Clear buffer
    memset(led_buffer, 0, sizeof(led_buffer));
    ESP_LOGI(TAG, "WS2812B initialized with RMT");
}

void ws2812b_set_color(uint16_t index, uint8_t r, uint8_t g, uint8_t b)
{
    if (index >= WS2812B_NUM_LEDS) return;

    // Store RGB in buffer (WS2812B uses GRB color order)
    led_buffer[index * 3 + 0] = g;
    led_buffer[index * 3 + 1] = r;
    led_buffer[index * 3 + 2] = b;
}

void ws2812b_set_all(uint8_t r, uint8_t g, uint8_t b)
{
    for (uint16_t i = 0; i < WS2812B_NUM_LEDS; i++) {
        ws2812b_set_color(i, r, g, b);
    }
}

void ws2812b_update()
{
    if (!rmt_channel || !rmt_encoder) {
        ESP_LOGW(TAG, "RMT not initialized");
        return;
    }

    rmt_transmit_config_t tx_conf = {
        .loop_count = 0,
    };

    esp_err_t ret = rmt_transmit(rmt_channel, rmt_encoder, led_buffer, 
                                  sizeof(led_buffer), &tx_conf);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "RMT transmit failed: %s", esp_err_to_name(ret));
        return;
    }

    rmt_tx_wait_all_done(rmt_channel, portMAX_DELAY);
    esp_rom_delay_us(60);
}

void ws2812b_clear()
{
    ws2812b_set_all(0, 0, 0);
    ws2812b_update();
}
