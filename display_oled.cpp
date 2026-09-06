#include "display_oled.h"
#include "config.h"

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// Objeto OLED
Adafruit_SSD1306 display_oled(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

void displayOledInit()
{
    Wire.begin(21, 22);

    if (!display_oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR))
    {
        Serial.println(F("SSD1306 allocation failed"));
        for(;;); // Infinite loop to terminate program
    }

    display_oled.clearDisplay();
    display_oled.display();
}

void displayOledShowBuffer(uint8_t* buffer, uint16_t width, uint16_t height)
{
    display_oled.clearDisplay();

    display_oled.drawBitmap(
        0,
        0,
        buffer,
        width,
        height,
        SSD1306_WHITE
    );

    display_oled.display();
}

void displayOledShowBufferScaled(uint8_t* buffer, uint16_t width, uint16_t height)
{
    uint8_t scaleX = SCREEN_WIDTH / width;
    uint8_t scaleY = SCREEN_HEIGHT / height;

    uint8_t scale = min(scaleX, scaleY);

    if (scale < 1)
        scale = 1;

    uint16_t scaledWidth = width * scale;
    uint16_t scaledHeight = height * scale;

    int16_t offsetX =
        (SCREEN_WIDTH - scaledWidth) / 2;

    int16_t offsetY =
        (SCREEN_HEIGHT - scaledHeight) / 2;

    display_oled.clearDisplay();

    for (uint16_t y = 0; y < height; y++)
    {
        for (uint16_t x = 0; x < width; x++)
        {
            uint16_t pixelIndex = y * width + x;
            uint8_t byte = buffer[pixelIndex / 8];

            uint8_t bit = pixelIndex % 8;
            bool pixel = byte & (1 << bit);

            if (pixel)
            {
                display_oled.fillRect(
                    offsetX + x * scale,
                    offsetY + y * scale,
                    scale,
                    scale,
                    SSD1306_WHITE
                );
            }
        }
    }

    display_oled.display();
}