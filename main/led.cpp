
#include "led.h"
#include "driver/rmt_tx.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include <cstring>


const static char* TAG = "LED";


class LEDDriver {
public:
  virtual void set_led(bool r, bool g, bool b) = 0;
};

class LEDGPIODriver : public virtual LEDDriver {
private:
  const uint64_t LED_RED   = 32;
  const uint64_t LED_GREEN = 22;
  const uint64_t LED_BLUE  = 23;
  const uint64_t LED_SEL_MASK = ((1ULL << LED_RED) | (1ULL << LED_GREEN) | (1ULL << LED_BLUE));

public:
  LEDGPIODriver() {
    gpio_config_t gpioConfig;

    // Configure LED
    gpioConfig.pin_bit_mask = LED_SEL_MASK;
    gpioConfig.mode = GPIO_MODE_OUTPUT;
    gpioConfig.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&gpioConfig);
  }

  void set_led(bool r, bool g, bool b) {
    gpio_set_level((gpio_num_t) LED_RED, r);
    gpio_set_level((gpio_num_t) LED_GREEN, g);
    gpio_set_level((gpio_num_t) LED_BLUE, b);
  }
};

class LEDWS2812Driver : public virtual LEDDriver {
private:
  // WS2812B timing: 800kHz data rate
  // Using RMT with 8MHz clock = 125ns resolution
  // T0H: 350ns ≈ 3 ticks, T0L: 900ns ≈ 7 ticks
  // T1H: 700ns ≈ 6 ticks, T1L: 600ns ≈ 5 ticks

  const static int WS2812B_GPIO = 25;
  const static int WS2812B_NUM_LEDS = 1;
  const static int WS2812B_RMT_CLK_KHZ = 8000;  // 8MHz clock
  const static int WS2812B_DEFAULT_BRIGHTNESS = 0x55;

  // RMT handles
  rmt_channel_handle_t rmt_channel = nullptr;
  rmt_encoder_handle_t rmt_encoder = nullptr;

  // Buffer to hold RGB data
  uint8_t led_buffer[WS2812B_NUM_LEDS * 3]; // RGB bytes


  void ws2812b_set_color(uint16_t index, uint8_t r, uint8_t g, uint8_t b) {
      if (index >= WS2812B_NUM_LEDS) return;
      
      // Store RGB in buffer (WS2812B uses GRB color order)
      led_buffer[index * 3 + 0] = g;
      led_buffer[index * 3 + 1] = r;
      led_buffer[index * 3 + 2] = b;
  }

  void ws2812b_set_all(uint8_t r, uint8_t g, uint8_t b) {
      for (uint16_t i = 0; i < WS2812B_NUM_LEDS; i++) {
          ws2812b_set_color(i, r, g, b);
      }
  }

  void ws2812b_update() {
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

public:
  LEDWS2812Driver() {
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

  void set_led(bool r, bool g, bool b) {
    ws2812b_set_all(r ? WS2812B_DEFAULT_BRIGHTNESS : 0,
                    g ? WS2812B_DEFAULT_BRIGHTNESS : 0,
                    b ? WS2812B_DEFAULT_BRIGHTNESS : 0);
    ws2812b_update();
  }
};



#define BIT_R BIT0
#define BIT_G BIT1
#define BIT_B BIT2

#define BIT_BLINK BIT3
#define BIT_AUTO_OFF BIT4

#define BIT_TRIGGER BIT5

static TaskHandle_t task_handle;
static EventGroupHandle_t event_group;


static void led_task(void* p)
{
  TickType_t delay = portMAX_DELAY;
  uint32_t r = 0;
  uint32_t g = 0;
  uint32_t b = 0;
  bool on = false;
  bool auto_off = false;

#ifdef CONFIG_STRIPBOARD
  LEDDriver* driver = new LEDGPIODriver();
#else
  LEDDriver* driver = new LEDWS2812Driver();
#endif
  
  while(true) {
    EventBits_t bits = xEventGroupWaitBits(event_group,
                                           BIT_R | BIT_G | BIT_B |
                                           BIT_BLINK | BIT_AUTO_OFF |
                                           BIT_TRIGGER,
                                           pdTRUE, // Clear on exit
                                           pdFALSE, // Wait for all bits
                                           delay);
    if (bits != 0) {
      r = (bits & BIT_R) ? 1 : 0;
      g = (bits & BIT_G) ? 1 : 0;
      b = (bits & BIT_B) ? 1 : 0;
      delay = (bits & BIT_BLINK) ? 500 / portTICK_PERIOD_MS : portMAX_DELAY;
      auto_off = bits & BIT_AUTO_OFF;
      if (auto_off) {
        delay = 3000 / portTICK_PERIOD_MS;
      }
      on = true;
    }

    if (on) {
      driver->set_led(r, g, b);
    } else {
      driver->set_led(false, false, false);
      if (auto_off) {
        delay = portMAX_DELAY;
      }
    }
    on = !on;
  }
}

void set_led(bool r, bool g, bool b, bool blink, bool auto_off)
{
  EventBits_t mask = BIT_TRIGGER;

  if (r) {
    mask |= BIT_R;
  }
  if (g) {
    mask |= BIT_G;
  }
  if (b) {
    mask |= BIT_B;
  }
  if (blink) {
    mask |= BIT_BLINK;
  }
  if (auto_off) {
    mask |= BIT_AUTO_OFF;
  }
  xEventGroupSetBits(event_group, mask);
}

void init_led()
{
  event_group = xEventGroupCreate();
  xEventGroupClearBits(event_group, 0xff);
  xTaskCreatePinnedToCore(led_task, "led", 2048, NULL, 1, &task_handle, 0);
  
  // Turn LED off
  set_led(false, false, false, false, false);
}
