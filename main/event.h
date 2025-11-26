
#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#define EVT_WIFI_NOT_INIT      (1 << 0)
#define EVT_WIFI_UP            (1 << 1)
#define EVT_WIFI_DOWN          (1 << 2)
#define EVT_CHECK_20K          (1 << 3)
#define EVT_BUTTON_SHORT_PRESS (1 << 4)
#define EVT_BUTTON_LONG_PRESS  (1 << 5)

void evt_signal(EventBits_t bits);
void evt_signal_isr(EventBits_t bits);
void init_events();
