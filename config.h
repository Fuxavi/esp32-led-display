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

#define SD_CS      5    // Pin de Slave Select (CS) para la tarjeta MicroSD
#define SD_SCK     18   // Pin de SCK para la tarjeta MicroSD
#define SD_MISO    19   // Pin de MISO para la tarjeta MicroSD
#define SD_MOSI    23   // Pin de MOSI para la tarjeta MicroSD

// LED panel pins

#define R1_PIN  25
#define G1_PIN  26
#define B1_PIN  27

#define R2_PIN  14
#define G2_PIN  12
#define B2_PIN  13

#define A_PIN   22
#define B_PIN   1
#define C_PIN   3
#define D_PIN   17 // (TX2)
#define E_PIN   -1

#define LAT_PIN 4
#define OE_PIN  15
#define CLK_PIN 16 //(RX2)

#endif