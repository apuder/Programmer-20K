

#include <cstdint>

#include "event.h"
#include "wifi.h"
#include "led.h"

#define EVT_ALL_EVENTS (EVT_WIFI_NOT_INIT | EVT_WIFI_UP | EVT_WIFI_DOWN | EVT_CHECK_20K | \
                        EVT_BUTTON_SHORT_PRESS | EVT_BUTTON_LONG_PRESS | EVT_UPLOAD_START | \
                        EVT_UPLOAD_SUCCESS | EVT_UPLOAD_FAIL)


static EventGroupHandle_t eg;
static TaskHandle_t event_task_handle = nullptr;

void evt_signal(EventBits_t bits)
{
  xEventGroupSetBits(eg, bits);
}

void evt_signal_isr(EventBits_t bits)
{
  xEventGroupSetBitsFromISR(eg, bits, nullptr);
}

EventBits_t evt_wait(EventBits_t bits)
{
  return xEventGroupWaitBits(eg, bits, pdTRUE, pdFALSE, portMAX_DELAY) & bits;
}

EventBits_t evt_check(EventBits_t bits)
{
  return xEventGroupWaitBits(eg, bits, pdTRUE, pdFALSE, 0) & bits;
}

static void event_task(void* arg)
{
  while (1) {
    EventBits_t evt = xEventGroupWaitBits(eg, EVT_ALL_EVENTS, pdTRUE, pdFALSE, portMAX_DELAY);
    if (evt & EVT_WIFI_UP) {
      // Handle WIFI_UP event. Flash LED green once
      set_led(false, true, false, false, true);
    }
    if (evt & EVT_WIFI_DOWN) {
      // Handle WIFI_DOWN event. Set LED to red
      set_led(true, false, false, false, false);
    }
    if (evt & EVT_WIFI_NOT_INIT) {
      // Handle WIFI_NOT_INIT event. Set LED to flashing white
      set_led(true, true, true, true, false);
    }
    if (evt & EVT_BUTTON_LONG_PRESS) {
      // Reset Wi‑Fi configuration
      wifi_reset();
    }
    if (evt & EVT_UPLOAD_START) {
      // Indicate upload start with blue LED
      set_led(false, false, true, false, false);
    }
    if (evt & EVT_UPLOAD_SUCCESS) {
      // Indicate upload success with green LED
      set_led(false, true, false, false, true);
    }
    if (evt & EVT_UPLOAD_FAIL) {
      // Indicate upload failure with red LED
      set_led(true, false, false, false, true);
    }
  }
}

void init_events()
{
  eg = xEventGroupCreate();
  xTaskCreatePinnedToCore(event_task, "event_task", 2048, NULL, 5, &event_task_handle, tskNO_AFFINITY);
}