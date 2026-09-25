#include "animation.h"
#include "display_oled.h"
#include "display_led.h"
#include "config.h"
#include <Audio.h>

// ============================================================
// I2S - MAX98357A
// ============================================================

#define I2S_BCLK 32
#define I2S_LRC  33
#define I2S_DOUT 2

Audio audio;


// ============================================================
// ARCHIVOS
// ============================================================

File animationFile;
File audioFile;


// ============================================================
// ANIMACIÓN
// ============================================================

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
// INICIALIZAR I2S
// ============================================================

void initAudio()
{
    audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
    audio.setVolume(100);
}

// ============================================================
// PARAR AUDIO
// ============================================================

void stopAudio()
{
    audio.stopSong();
}

void startAudio(String filename) {
    audio.stopSong();
    audio.connecttoFS(SD, filename.c_str());
}

void updateAudio()
{
    audio.loop();
}

// INICIAR ANIMACIÓN

void startAnimation(String filename, bool ledDisplay) {
    // Si ya había una animación reproduciéndose, la detenemos primero.
    stopAnimation();
    animationFile = SD.open(filename, "r");

    if (!animationFile) {
        ////Serial.println("No se pudo abrir la animacion");
        return;
    }

    if (animationFile.read((uint8_t*)&animationWidth, 2) != 2 ||
        animationFile.read((uint8_t*)&animationHeight, 2) != 2 ||
        animationFile.read((uint8_t*)&animationNumFrames, 2) != 2 ||
        animationFile.read((uint8_t*)&animationFPS, 2) != 2) {

        //Serial.println("Error leyendo cabecera");
        stopAnimation();
        return;
    }

    /*if (animationWidth == 0 ||
        animationHeight == 0 ||
        animationWidth > SCREEN_WIDTH ||
        animationHeight > SCREEN_HEIGHT) {

        //Serial.println("Dimensiones invalidas");
        stopAnimation();
        return;
    }*/

    if (animationNumFrames == 0) {
        //Serial.println("Numero de frames invalido");
        stopAnimation();
        return;
    }

    // Tamaño de un frame
    if (ledDisplay) animationFrameSize = animationWidth * animationHeight * 2; // 2 bytes por pixel (RGB565)
    else animationFrameSize = ((animationWidth * animationHeight) + 7) / 8;

    size_t expectedSize = 8 + animationFrameSize * animationNumFrames;

    if (animationFile.size() != expectedSize) {
        //Serial.println("Tamano de archivo incorrecto");
        //Serial.printf("Esperados: %d bytes\n", expectedSize);
        //Serial.printf("Recibidos: %d bytes\n",animationFile.size());
        stopAnimation();
        return;
    }

    // Reservar memoria para UN solo frame
    animationBuffer = new uint8_t[animationFrameSize];

    if (animationBuffer == nullptr) {
        //Serial.println("No hay memoria suficiente");
        stopAnimation();
        return;
    }

    // Calcular tiempo entre frames
    if (animationFPS > 0) animationFrameDelay = 1000 / animationFPS;
    else animationFrameDelay = 0;

    animationCurrentFrame = 0;
    animationLastFrameTime = millis();
    animationPlaying = true;

    animationFile.seek(8);

    if (animationFile.read(
            animationBuffer,
            animationFrameSize
        ) != animationFrameSize)
    {
        stopAnimation();
        return;
    }

    if (ledDisplay) {
        //Serial.println("Showing on LED display");
            displayLedShowBufferScaled(
            animationBuffer,
            animationWidth,
            animationHeight
        );
    }
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
        ////Serial.println("Error leyendo frame");
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

void stopAnimation()
{
    animationPlaying = false;

    // Cerrar archivo de animación
    if (animationFile) animationFile.close();

    // Liberar frame
    if (animationBuffer != nullptr) {
        delete[] animationBuffer;
        animationBuffer = nullptr;
    }

    // Reset
    animationCurrentFrame = 0;
}

// ============================================================
// REPRODUCIR AUDIO DE PRUEBA
// ============================================================

bool startTestAudio()
{
    // Inicializar I2S
    if (!initAudio()) {
        Serial.println("ERROR: No se pudo inicializar I2S");
        return false;
    }

    // Abrir archivo
    testAudioFile = SD.open("/test.wav", FILE_READ);

    if (!testAudioFile) {
        Serial.println("ERROR: No se pudo abrir /test.wav");
        return false;
    }

    Serial.println("Reproduciendo /test.wav");

    // --------------------------------------------------------
    // IMPORTANTE:
    // Si es WAV, saltamos la cabecera WAV.
    // Una cabecera WAV PCM normal suele ocupar 44 bytes.
    // --------------------------------------------------------

    testAudioFile.seek(44);

    testAudioPlaying = true;

    return true;
}

// ============================================================
// ACTUALIZAR AUDIO DE PRUEBA
// ============================================================

void updateTestAudio()
{
    if (!testAudioPlaying) {
        return;
    }

    if (!testAudioFile) {
        testAudioPlaying = false;
        return;
    }

    // Leer bloque de audio
    size_t bytesRead = testAudioFile.read(
        testAudioBuffer,
        sizeof(testAudioBuffer)
    );

    // --------------------------------------------------------
    // Fin del archivo
    // --------------------------------------------------------

    if (bytesRead == 0) {

        Serial.println("Fin del audio");

        testAudioFile.seek(44);

        return;
    }

    // --------------------------------------------------------
    // Enviar PCM directamente al I2S
    // --------------------------------------------------------

    size_t bytesWritten = 0;

    esp_err_t err = i2s_channel_write(
        i2sTxHandle,
        testAudioBuffer,
        bytesRead,
        &bytesWritten,
        10
    );

    if (err != ESP_OK) {

        Serial.print("ERROR I2S: ");
        Serial.println((int)err);
    }
}
