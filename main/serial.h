
#pragma once

#define UART_PORT       UART_NUM_2
#ifdef CONFIG_STRIPBOARD
#define UART_RX_PIN     16          // GPIO16 = RX
#define UART_TX_PIN     17          // GPIO17 = TX
#else
#define UART_RX_PIN     5           // GPIO5 = RX
#define UART_TX_PIN     18          // GPIO18 = TX
#endif
#define BUF_SIZE        1024

void start_serial_monitor();
void stop_serial_monitor();
void serial_transmit(const uint8_t *data, size_t len);
