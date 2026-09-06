#ifndef DISPLAY_OLED_H
#define DISPLAY_OLED_H

#include <Arduino.h>

void displayOledInit();

void displayOledShowBuffer(uint8_t* buffer, uint16_t width, uint16_t height);

void displayOledShowBufferScaled(uint8_t* buffer, uint16_t width, uint16_t height);

#endif