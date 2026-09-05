#include "display.h"
#include "config.h"

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// Objeto OLED
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

void displayInit()
{
    Wire.begin(21, 22);

    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR))
    {
        Serial.println(F("SSD1306 allocation failed"));
        for(;;); // Infinite loop to terminate program
    }

    display.clearDisplay();
    display.display();
}

void showBuffer(uint8_t* buffer, uint16_t width, uint16_t height){
    // Mostrar imagen
    display.clearDisplay();

    display.drawBitmap(
        0,
        0,
        buffer,
        width,
        height,
        SSD1306_WHITE
    );

    display.display();
}

void showBufferScaled(uint8_t* buffer, uint16_t width, uint16_t height) {

    // Calcular escala máxima que cabe en la pantalla
    uint8_t scaleX = SCREEN_WIDTH / width;
    uint8_t scaleY = SCREEN_HEIGHT / height;

    uint8_t scale = min(scaleX, scaleY);

    if (scale < 1) {
        scale = 1;
    }

    Serial.printf("Escala: x%d\n", scale);

    // Dimensiones finales
    uint16_t scaledWidth = width * scale;
    uint16_t scaledHeight = height * scale;

    // Centrar la imagen
    int16_t offsetX = (SCREEN_WIDTH - scaledWidth) / 2;
    int16_t offsetY = (SCREEN_HEIGHT - scaledHeight) / 2;

    display.clearDisplay();

    // Recorrer cada píxel de la imagen original
    for (uint16_t y = 0; y < height; y++) {

        for (uint16_t x = 0; x < width; x++) {

            uint16_t pixelIndex = y * width + x;

            uint8_t byte = buffer[pixelIndex / 8];
            uint8_t bit = pixelIndex % 8;

            bool pixel = byte & (1 << bit);

            if (pixel) {

                // Dibujar el píxel escalado
                display.fillRect(
                    offsetX + x * scale,
                    offsetY + y * scale,
                    scale,
                    scale,
                    SSD1306_WHITE
                );
            }
        }
    }

    display.display();
}