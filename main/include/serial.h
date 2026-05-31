
#pragma once

#include <inttypes.h>
#include <cstddef>

#define UART_PORT       UART_NUM_2

#if defined(CONFIG_STRIPBOARD)
#define UART_RX_PIN     16          // GPIO16 = RX
#define UART_TX_PIN     17          // GPIO17 = TX
#elif defined(CONFIG_MINI_PC)
#define UART_RX_PIN     35
#define UART_TX_PIN     15
#else
#define UART_RX_PIN     16
#define UART_TX_PIN     17
#endif

#define BUF_SIZE        1024

void start_serial_monitor();
void stop_serial_monitor();
void serial_transmit(const uint8_t *data, size_t len);