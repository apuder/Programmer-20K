
#pragma once

#define UART_PORT       UART_NUM_2
#define UART_RX_PIN     16          // GPIO16 = RX
#define UART_TX_PIN     17          // GPIO17 = TX
#define BUF_SIZE        1024

void start_serial_monitor();
void stop_serial_monitor();
void serial_transmit(const uint8_t *data, size_t len);
