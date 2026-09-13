#ifndef CONFIG_H
#define CONFIG_H

// OLED
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_ADDR 0x3C

// HUB75
#define LED_WIDTH 64
#define LED_HEIGHT 32
#define LED_CHAIN 1

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

// ==================== LED ====================
#define LED_PIN 2              // Onboard LED, controlled by /ledon and /ledoff commands

// SD card pins

#define SD_CS    21
#define SD_MOSI  33
#define SD_SCK   32
#define SD_MISO  35

#endif