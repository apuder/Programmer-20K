/* Hello World Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
//#include "esp_spi_flash.h"
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_err.h"
#include "jtag.h"
#include "button.h"
#include "led.h"
#include "event.h"
#include "flash.h"

static const char *TAG = "Programmer-20K";

#define UART_PORT       UART_NUM_2
#define UART_RX_PIN     16          // GPIO16 = RX
#define UART_TX_PIN     17          // TX (unused but required by driver)
#define BUF_SIZE        1024


extern esp_err_t start_http_server();


extern "C" void app_main(void)
{
    init_events();
    init_button();
    init_led();
    JTAGAdapter* jtag = new JTAGAdapter();
    // Section 2.2.5, IDCODE for GW2A(R)-18
    int err = jtag->scan(8, 1, 0x0000081B);
    ESP_LOGE("JTAG", "Scan result: %d", err);
    delete jtag;

#if 1
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
        /* Stop here to avoid using an unconfigured UART */
        while (1) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    // Set pins (TX must be assigned even if unused)
    ret = uart_set_pin(UART_PORT, UART_TX_PIN, UART_RX_PIN,
                      UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "uart_set_pin failed: %s", esp_err_to_name(ret));
        while (1) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    // Install UART driver
    ret = uart_driver_install(UART_PORT, BUF_SIZE, 0, 0, NULL, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "uart_driver_install failed: %s", esp_err_to_name(ret));
        while (1) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    // start the HTTP server (non-blocking)
    start_http_server();

    // Move UART polling to a dedicated task so the main thread can keep other services alive
    static const char *UART_TASK_TAG = "uart_reader";

    auto uart_task = [](void *arg) -> void {
        uint8_t data[BUF_SIZE];
        uint8_t value = 0;
        while (1) {
#if 1
            printf("Cookie: 0x%02X\n", spi_get_cookie());
#if 1
            unsigned long id = flashReadMfdDevId();
            printf("Flash ID: (0x%lx) %s\n", id, flashMfdDevIdStr(id));
#endif
            vTaskDelay(pdMS_TO_TICKS(1000));
#else
            int len = uart_read_bytes(UART_PORT, data, sizeof(data), pdMS_TO_TICKS(100));
            if (len > 0) {
                for (int i = 0; i < len; i++) putchar((char)data[i]);
                fflush(stdout);
            }
            vTaskDelay(pdMS_TO_TICKS(10));
#endif
        }
    };

    xTaskCreatePinnedToCore((TaskFunction_t)uart_task, "uart_reader", 4096, NULL, 4, NULL, tskNO_AFFINITY);
#endif
    vTaskDelete(NULL);
}
