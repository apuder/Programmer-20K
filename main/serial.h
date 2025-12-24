
#pragma once

#define UART_PORT       UART_NUM_2
#define UART_RX_PIN     16          // GPIO16 = RX
#define UART_TX_PIN     17          // TX (unused but required by driver)
#define BUF_SIZE        1024

void start_serial_monitor();
void stop_serial_monitor();
