
#include "event.h"
#include "button.h"
#include "driver/gpio.h"
#include "esp_event.h"
#include "esp_timer.h"
#include "esp_intr_alloc.h"

#ifdef CONFIG_STRIPBOARD
#define GPIO_BUTTON GPIO_NUM_36
#else
#define GPIO_BUTTON GPIO_NUM_0
#endif


static bool is_status_button_pressed()
{
  return gpio_get_level(GPIO_BUTTON) == 0;
}


static void IRAM_ATTR isr_button(void* arg)
{
  static int64_t then = INT64_MAX;
  
  if (is_status_button_pressed()) {
    then = esp_timer_get_time();
  } else {
    int64_t now = esp_timer_get_time();
    int64_t delta_ms = (now - then) / 1000;
    then = INT64_MAX;
    if (delta_ms < 20) {
      // Bounce
      return;
    }
    if (delta_ms < 300) {
      evt_signal_isr(EVT_BUTTON_SHORT_PRESS);
    }
    if (delta_ms > 3000) {
      evt_signal_isr(EVT_BUTTON_LONG_PRESS);
    }
  }
}

void init_button()
{
  gpio_config_t gpioConfig;

  // Configure push button
  gpioConfig.pin_bit_mask = (1ULL << GPIO_BUTTON);
  gpioConfig.mode = GPIO_MODE_INPUT;
  gpioConfig.pull_up_en = GPIO_PULLUP_DISABLE;
  gpioConfig.pull_down_en = GPIO_PULLDOWN_DISABLE;
  gpioConfig.intr_type = GPIO_INTR_ANYEDGE;
  gpio_config(&gpioConfig);

  gpio_install_isr_service(0); //XXX
  gpio_isr_handler_add(GPIO_BUTTON, isr_button, NULL);
}
