#ifndef DISPLAY_LED_H
#define DISPLAY_LED_H

#include <Arduino.h>

void displayLedInit();

void displayLedShowBuffer(uint8_t* buffer, uint16_t width, uint16_t height);

void displayLedShowBufferScaled(uint8_t* buffer, uint16_t width, uint16_t height);

#endif