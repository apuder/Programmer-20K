// wifi.h — Wi‑Fi helper and HTTP handler registration
#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

// Initialize Wi‑Fi subsystem (netif, event loop, wifi driver). Safe to call repeatedly.
esp_err_t wifi_init_base();

// Start AP mode (open AP with default name). Returns ESP_OK on success.
esp_err_t wifi_start_ap();

// Start STA mode using provided credentials (ssid, password may be NULL).
esp_err_t wifi_start_sta(const char* ssid, const char* password);

// Register HTTP endpoints for /wifi POST and DELETE onto the running httpd instance.
// This keeps Wi‑Fi handling separated from the HTTP layer implementation.
esp_err_t register_wifi_http_handlers(httpd_handle_t server);

// Optionally unregister handlers
esp_err_t unregister_wifi_http_handlers(httpd_handle_t server);

void wifi_reset();

// Check if WiFi is configured in STA mode (credentials exist in NVS)
bool wifi_is_sta_mode();

void init_wifi();
