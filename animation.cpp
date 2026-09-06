#include "animation.h"
#include "display_oled.h"
#include "display_led.h"
#include "config.h"

File animationFile;

uint8_t* animationBuffer = nullptr;

uint16_t animationWidth = 0;
uint16_t animationHeight = 0;
uint16_t animationNumFrames = 0;
uint16_t animationFPS = 0;

size_t animationFrameSize = 0;

uint16_t animationCurrentFrame = 0;

uint32_t animationLastFrameTime = 0;
uint32_t animationFrameDelay = 0;

bool animationPlaying = false;
bool usingLedDisplay = false;

// ============================================================
// Iniciar animación
// ============================================================

void startAnimation(String filename, bool ledDisplay) {
    // Si ya había una animación reproduciéndose, la detenemos primero.
    stopAnimation();
    animationFile = LittleFS.open(filename, "r");

    if (!animationFile) {
        Serial.println("No se pudo abrir la animacion");
        return;
    }

    if (animationFile.read((uint8_t*)&animationWidth, 2) != 2 ||
        animationFile.read((uint8_t*)&animationHeight, 2) != 2 ||
        animationFile.read((uint8_t*)&animationNumFrames, 2) != 2 ||
        animationFile.read((uint8_t*)&animationFPS, 2) != 2) {

        Serial.println("Error leyendo cabecera");
        stopAnimation();
        return;
    }

    if (animationWidth == 0 ||
        animationHeight == 0 ||
        animationWidth > SCREEN_WIDTH ||
        animationHeight > SCREEN_HEIGHT) {

        Serial.println("Dimensiones invalidas");
        stopAnimation();
        return;
    }

    if (animationNumFrames == 0) {
        Serial.println("Numero de frames invalido");
        stopAnimation();
        return;
    }

    // Tamaño de un frame
    if (ledDisplay) animationFrameSize = animationFrameSize = animationWidth * animationHeight * 2; // 2 bytes por pixel (RGB565)
    else animationFrameSize = ((animationWidth * animationHeight) + 7) / 8;

    size_t expectedSize = 8 + animationFrameSize * animationNumFrames;

    if (animationFile.size() != expectedSize) {
        Serial.println("Tamano de archivo incorrecto");
        Serial.printf("Esperados: %d bytes\n", expectedSize);
        Serial.printf("Recibidos: %d bytes\n",animationFile.size());
        stopAnimation();
        return;
    }

    // Reservar memoria para UN solo frame
    animationBuffer = new uint8_t[animationFrameSize];

    if (animationBuffer == nullptr) {
        Serial.println("No hay memoria suficiente");
        stopAnimation();
        return;
    }

    // Calcular tiempo entre frames
    if (animationFPS > 0) animationFrameDelay = 1000 / animationFPS;
    else animationFrameDelay = 0;

    animationCurrentFrame = 0;
    animationLastFrameTime = millis();
    animationPlaying = true;

    // Cargar inmediatamente el primer frame
    if (animationFile.read(
            animationBuffer,
            animationFrameSize
        ) != animationFrameSize) {

        Serial.println("Error leyendo primer frame");

        stopAnimation();
        return;
    }

    if (ledDisplay) displayLedShowBufferScaled(
        animationBuffer,
        animationWidth,
        animationHeight
    );
    else displayOledShowBufferScaled(
        animationBuffer,
        animationWidth,
        animationHeight
    );
    usingLedDisplay = ledDisplay;
}

void updateAnimation() {
    if (!animationPlaying) {
      return;
    }
    uint32_t now = millis();
    // Todavía no toca cambiar de frame
    if (animationFrameDelay > 0 &&
      now - animationLastFrameTime < animationFrameDelay) {
      return;
    }

    animationLastFrameTime = now;
    animationCurrentFrame++;

    // Si llegamos al final, volvemos al principio (loop)
    if (animationCurrentFrame >= animationNumFrames) {
      animationCurrentFrame = 0;
      // Saltamos al primer frame, la cabecera ocupa 8 bytes.
      animationFile.seek(8);
    }

    if (animationFile.read(
            animationBuffer,
            animationFrameSize
        ) != animationFrameSize) {

        Serial.println("Error leyendo frame");

        stopAnimation();
        return;
    }
    if (usingLedDisplay) {
        displayLedShowBufferScaled(
            animationBuffer,
            animationWidth,
            animationHeight
        );
    } else displayOledShowBufferScaled(
        animationBuffer,
        animationWidth,
        animationHeight
    );
}

void stopAnimation() {
    animationPlaying = false;
    if (animationFile) {
        animationFile.close();
    }

    if (animationBuffer != nullptr) {
        delete[] animationBuffer;
        animationBuffer = nullptr;
    }
    animationCurrentFrame = 0;
}