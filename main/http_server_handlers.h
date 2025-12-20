#pragma once

#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t start_http_server();
esp_err_t stop_http_server();
void log_serial_monitor(uint8_t *data, int len);

#ifdef __cplusplus
}
#endif
