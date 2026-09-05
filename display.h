#ifndef DISPLAY_H
#define DISPLAY_H

#include <Arduino.h>

void displayInit();
void showBuffer(uint8_t* buffer, uint16_t width, uint16_t height);
void showBufferScaled(uint8_t* buffer, uint16_t width, uint16_t height);

#endif