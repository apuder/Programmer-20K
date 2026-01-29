
#include "led.h"
#include "driver/rmt.h"
#include "driver/gpio.h"
#include "esp_log.h"
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
  // 0 bit: 0.4µs high + 0.85µs low = 1.25µs total
  // 1 bit: 0.8µs high + 0.45µs low = 1.25µs total
  // Using RMT for precise hardware-controlled timing
  // Clock divider: 80MHz / 4 = 20MHz (50ns resolution)
  // 0 bit: 8 ticks high (0.4µs) + 17 ticks low (0.85µs)
  // 1 bit: 16 ticks high (0.8µs) + 9 ticks low (0.45µs)

  const static int WS2812B_GPIO = 25;
  const static int WS2812B_NUM_LEDS = 1;
  const static int WS2812B_RMT_CLK_DIV = 4;
  const static int WS2812B_DEFAULT_BRIGHTNESS = 0x55;
  const static rmt_channel_t WS2812B_RMT_CHANNEL = RMT_CHANNEL_0;

  // RMT items for 0 and 1 bits (ticks @ 50ns resolution)
  // RMT item format: {{{ duration0, level0, duration1, level1 }}}
  // Each duration is in ticks (50ns per tick @ 20MHz clock)
  const rmt_item32_t bit0 = {{{ 8, 1, 17, 0 }}}; // WS2812B '0': 8*50ns=0.4µs high, 17*50ns=0.85µs low (1.25µs total)
  const rmt_item32_t bit1 = {{{ 16, 1, 9, 0 }}}; // WS2812B '1': 16*50ns=0.8µs high, 9*50ns=0.45µs low (1.25µs total)

  // Buffer to hold RGB data and converted RMT items
  uint8_t led_buffer[WS2812B_NUM_LEDS * 3]; // RGB bytes
  rmt_item32_t rmt_items[WS2812B_NUM_LEDS * 24]; // RMT items (24 bits per LED)

  void convert_to_rmt_items() {
      for (int led = 0; led < WS2812B_NUM_LEDS; led++) {
          // WS2812B color order is GRB (Green, Red, Blue)
          uint8_t g = led_buffer[led * 3 + 0];
          uint8_t r = led_buffer[led * 3 + 1];
          uint8_t b = led_buffer[led * 3 + 2];
          
          uint8_t grb[3] = {g, r, b};
          
          // Convert each byte to 8 RMT items (one per bit)
          for (int byte_idx = 0; byte_idx < 3; byte_idx++) {
              uint8_t byte = grb[byte_idx];
              for (int bit = 0; bit < 8; bit++) {
                  int item_idx = (led * 24) + (byte_idx * 8) + bit;
                  // MSB first
                  if (byte & (1 << (7 - bit))) {
                      rmt_items[item_idx] = bit1;
                  } else {
                      rmt_items[item_idx] = bit0;
                  }
              }
          }
      }
  }

  void ws2812b_set_color(uint16_t index, uint8_t r, uint8_t g, uint8_t b) {
      if (index >= WS2812B_NUM_LEDS) return;
      
      // Store RGB in buffer (will convert to GRB during RMT conversion)
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
      // Convert RGB buffer to RMT items
      convert_to_rmt_items();
      
      // Send via RMT
      esp_err_t ret = rmt_write_items(WS2812B_RMT_CHANNEL, rmt_items, WS2812B_NUM_LEDS * 24, true);
      if (ret != ESP_OK) {
          ESP_LOGW(TAG, "RMT write failed: %s", esp_err_to_name(ret));
      }
  }

public:
  LEDWS2812Driver() {
    ESP_LOGI(TAG, "Initializing WS2812B on GPIO %d using RMT", WS2812B_GPIO);
    
    rmt_config_t config = RMT_DEFAULT_CONFIG_TX((gpio_num_t)WS2812B_GPIO, WS2812B_RMT_CHANNEL);
    config.clk_div = WS2812B_RMT_CLK_DIV;
    
    esp_err_t ret = rmt_config(&config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "RMT config failed: %s", esp_err_to_name(ret));
        return;
    }
    
    ret = rmt_driver_install(config.channel, 0, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "RMT driver install failed: %s", esp_err_to_name(ret));
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
