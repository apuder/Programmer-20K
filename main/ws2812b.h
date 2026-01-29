#pragma once

#include <cstdint>

// WS2812B LED strip support via SPI
// Connected to GPIO 25 (single LED)

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} ws2812b_color_t;

// Initialize WS2812B driver
void ws2812b_init();

// Set color of LED at index (0 for single LED)
// RGB order: Red, Green, Blue
void ws2812b_set_color(uint16_t index, uint8_t r, uint8_t g, uint8_t b);

// Set all LEDs to the same color
void ws2812b_set_all(uint8_t r, uint8_t g, uint8_t b);

// Update the LED strip (send buffered data)
void ws2812b_update();

// Turn off all LEDs
void ws2812b_clear();
