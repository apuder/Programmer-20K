


#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "serial.h"
#include "event.h"
#include "http_server_handlers.h"

static const char *TAG = "Serial";


static volatile bool run_uart_task = true;

void start_serial_monitor()
{
    // UART configuration: 115200-8-N-1
    uart_config_t uart_config = {0};
    uart_config.baud_rate = 115200;
    uart_config.data_bits = UART_DATA_8_BITS;
    uart_config.parity    = UART_PARITY_DISABLE;
    uart_config.stop_bits = UART_STOP_BITS_1;
    uart_config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    uart_config.source_clk = UART_SCLK_APB;

    // Apply config
    esp_err_t ret = uart_param_config(UART_PORT, &uart_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "uart_param_config failed: %s", esp_err_to_name(ret));
        return;
    }

    // Map UART signals to GPIOs
    ret = uart_set_pin(UART_PORT, UART_TX_PIN, UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "uart_set_pin failed: %s", esp_err_to_name(ret));
        return;
    }

    // Install UART driver
    ret = uart_driver_install(UART_PORT, BUF_SIZE, 0, 0, NULL, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "uart_driver_install failed: %s", esp_err_to_name(ret));
        return;
    }

    auto uart_task = [](void *arg) -> void {
        uint8_t data[BUF_SIZE];

        ESP_LOGI(TAG, "Serial monitor started");
        while (run_uart_task) {
            int len = uart_read_bytes(UART_PORT, data, sizeof(data), pdMS_TO_TICKS(100));
            if (len > 0) {
                // forward to websocket clients if connected
                log_serial_monitor(data, len);
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        uart_driver_delete(UART_PORT);
        evt_signal(EVT_STOP_UART_MONITOR);
        vTaskDelete(NULL);
    };

    run_uart_task = true;
    xTaskCreatePinnedToCore((TaskFunction_t) uart_task, "uart_reader", 4096, NULL, 4, NULL, tskNO_AFFINITY);
}

void stop_serial_monitor()
{
    ESP_LOGI(TAG, "Stopping serial monitor");
    run_uart_task = false;
    evt_wait(EVT_STOP_UART_MONITOR);
    ESP_LOGI(TAG, "Serial monitor stopped");
}

void serial_transmit(const uint8_t* data, size_t len)
{
    if (!run_uart_task || len == 0) return;

    int written = uart_write_bytes(UART_PORT, (const char*) data, len);
    if (written > 0) {
        uart_wait_tx_done(UART_PORT, pdMS_TO_TICKS(100));
    } else {
        ESP_LOGW(TAG, "uart_write_bytes failed (%d)", written);
    }
}