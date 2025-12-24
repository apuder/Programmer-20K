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
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_err.h"
#include "jtag.h"
#include "button.h"
#include "led.h"
#include "event.h"
#include "flash.h"
#include "serial.h"
#include "http_server_handlers.h"

static const char *TAG = "Programmer-20K";


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

    start_http_server();
    start_serial_monitor();
    vTaskDelete(NULL);
}
