#include "display_led.h"
#include "config.h"

#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>

HUB75_I2S_CFG::i2s_pins pins = {
    R1_PIN, G1_PIN, B1_PIN,
    R2_PIN, G2_PIN, B2_PIN,
    A_PIN, B_PIN, C_PIN, D_PIN,
    E_PIN,
    LAT_PIN, OE_PIN, CLK_PIN
};

// Objeto del display_led
MatrixPanel_I2S_DMA *display_led = nullptr;


void displayLedInit()
{
    // Configuración del panel
    HUB75_I2S_CFG mxconfig(
        LED_WIDTH,
        LED_HEIGHT,
        LED_CHAIN,
        pins
    );

    mxconfig.gpio.e = 18;
    mxconfig.driver = HUB75_I2S_CFG::FM6126A;
    mxconfig.clkphase = false;

    display_led = new MatrixPanel_I2S_DMA(mxconfig);
    ////Serial.println("Inicializando HUB75...");

    if (!display_led->begin())
    {
        ////Serial.println("Error inicializando HUB75");
        for (;;) delay(1000); //KILL
    }

    display_led->setBrightness8(50);
    display_led->clearScreen();

    display_led->fillScreen(display_led->color565(255,255,255));

    //Serial.println("HUB75 inicializado");
}


void displayLedShowBuffer(uint8_t* buffer, uint16_t width, uint16_t height)
{
    display_led->clearScreen();

    for (uint16_t y = 0; y < height; y++)
    {
        for (uint16_t x = 0; x < width; x++)
        {
            uint32_t pixelIndex = y * width + x;

            uint16_t color =
                buffer[pixelIndex * 2] |
                (buffer[pixelIndex * 2 + 1] << 8);

            display_led->drawPixel(x, y, color);
        }
    }
}

void displayLedShowBufferScaled(uint8_t* buffer, uint16_t width, uint16_t height)
{
    uint16_t scaleX = LED_WIDTH / width;
    uint16_t scaleY = LED_HEIGHT / height;

    uint16_t scale = min(scaleX, scaleY);
    if (scale < 1) scale = 1;


    uint16_t scaledWidth = width * scale;
    uint16_t scaledHeight = height * scale;

    int16_t offsetX = (LED_WIDTH - scaledWidth) / 2;

    int16_t offsetY = (LED_HEIGHT - scaledHeight) / 2;

    display_led->clearScreen();

    for (uint16_t y = 0; y < height; y++)
    {
        for (uint16_t x = 0; x < width; x++)
        {
            uint32_t pixelIndex =
                y * width + x;

            uint16_t color =
                buffer[pixelIndex * 2] |
                (buffer[pixelIndex * 2 + 1] << 8);

            display_led->fillRect(
                offsetX + x * scale,
                offsetY + y * scale,
                scale,
                scale,
                color
            );
        }
    }
}