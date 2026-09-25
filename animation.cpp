#include "animation.h"
#include "display_oled.h"
#include "display_led.h"
#include "config.h"
#include <driver/i2s_std.h>

// ============================================================
// I2S - MAX98357A
// ============================================================

#define I2S_BCLK 32
#define I2S_LRC  33
#define I2S_DOUT 2


#define AUDIO_SAMPLE_RATE 16000

// Pequeño bloque para no ocupar demasiado tiempo en cada loop.
// 512 bytes = 16 ms de audio a 16 kHz / 16 bit / mono.
#define AUDIO_BUFFER_SIZE 512

i2s_chan_handle_t i2sTxHandle = nullptr;

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
// AUDIO
// ============================================================

uint32_t audioOffset = 0;
uint32_t audioSize = 0;
uint32_t audioSampleRate = 0;

uint16_t audioChannels = 0;
uint16_t audioBits = 0;

uint32_t audioBytesPlayed = 0;
bool audioPlaying = false;

uint8_t audioBuffer[AUDIO_BUFFER_SIZE];


// ============================================================
// INICIALIZAR I2S
// ============================================================

bool initAudio()
{
    // Ya está inicializado
    if (i2sTxHandle != nullptr) {
        return true;
    }

    // --------------------------------------------------------
    // Crear canal TX
    // --------------------------------------------------------

    i2s_chan_config_t chan_cfg =
        I2S_CHANNEL_DEFAULT_CONFIG(
            I2S_NUM_0,
            I2S_ROLE_MASTER
        );

    esp_err_t err = i2s_new_channel(
        &chan_cfg,
        &i2sTxHandle,
        nullptr
    );

    if (err != ESP_OK) {

        i2sTxHandle = nullptr;

        return false;
    }

    // --------------------------------------------------------
    // Configuración I2S
    // --------------------------------------------------------
    //
    // Philips = I2S estándar.
    //
    // MAX98357A funciona con I2S estándar.
    //
    // --------------------------------------------------------

    i2s_std_config_t std_cfg = {

        .clk_cfg =
            I2S_STD_CLK_DEFAULT_CONFIG(
                AUDIO_SAMPLE_RATE
            ),

        .slot_cfg =
            I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
                I2S_DATA_BIT_WIDTH_16BIT,
                I2S_SLOT_MODE_MONO
            ),

        .gpio_cfg = {

            .mclk = I2S_GPIO_UNUSED,

            .bclk =
                (gpio_num_t)I2S_BCLK,

            .ws =
                (gpio_num_t)I2S_LRC,

            .dout =
                (gpio_num_t)I2S_DOUT,

            .din =
                I2S_GPIO_UNUSED,

            .invert_flags = {

                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false
            }
        }
    };

    // --------------------------------------------------------
    // Inicializar modo estándar
    // --------------------------------------------------------

    err = i2s_channel_init_std_mode(
        i2sTxHandle,
        &std_cfg
    );

    if (err != ESP_OK) {

        i2s_del_channel(i2sTxHandle);

        i2sTxHandle = nullptr;

        return false;
    }

    // --------------------------------------------------------
    // Activar canal
    // --------------------------------------------------------

    err = i2s_channel_enable(
        i2sTxHandle
    );

    if (err != ESP_OK) {

        i2s_del_channel(i2sTxHandle);

        i2sTxHandle = nullptr;

        return false;
    }

    // --------------------------------------------------------
    // ENVIAR SILENCIO INICIAL
    // --------------------------------------------------------
    //
    // 16 bits = 2 bytes por muestra
    // 16000 Hz = 16000 muestras/segundo
    //
    // 512 bytes = 256 muestras
    // 256 / 16000 = 16 ms
    //
    // 5 bloques = aproximadamente 80 ms
    //
    // --------------------------------------------------------

    uint8_t silence[512] = {0};

    size_t bytesWritten = 0;

    for (int i = 0; i < 5; i++) {

        err = i2s_channel_write(
            i2sTxHandle,
            silence,
            sizeof(silence),
            &bytesWritten,
            100
        );

        if (err != ESP_OK) {
            break;
        }
    }

    // --------------------------------------------------------
    // Inicialización correcta
    // --------------------------------------------------------

    return true;
}


// ============================================================
// PARAR AUDIO
// ============================================================

void stopAudio()
{
    audioPlaying = false;
    audioBytesPlayed = 0;
    if (audioFile) {
        audioFile.close();
    }
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

bool startAudio(String filename)
{
    // Inicializar I2S
    if (!initAudio()) {
        Serial.println("ERROR: No se pudo inicializar I2S");
        return false;
    }

    // Abrir archivo
    audioFile = SD.open(filename.c_str(), FILE_READ);

    if (!audioFile) {
        Serial.println("ERROR: No se pudo abrir " + filename);
        return false;
    }

    Serial.println("Reproduciendo "+ filename);

    // --------------------------------------------------------
    // IMPORTANTE:
    // Si es WAV, saltamos la cabecera WAV.
    // Una cabecera WAV PCM normal suele ocupar 44 bytes.
    // --------------------------------------------------------

    audioFile.seek(44);
    audioPlaying = true;

    return true;
}

// ============================================================
// ACTUALIZAR AUDIO DE PRUEBA
// ============================================================

void updateAudio()
{
    if (!audioPlaying) {
        return;
    }

    if (!audioFile) {
        audioPlaying = false;
        return;
    }

    // Leer bloque de audio
    size_t bytesRead = audioFile.read(
        audioBuffer,
        sizeof(audioBuffer)
    );

    // --------------------------------------------------------
    // Fin del archivo
    // --------------------------------------------------------

    if (bytesRead == 0) {
        Serial.println("Fin del audio");
        audioFile.seek(44);
        return;
    }

    // --------------------------------------------------------
    // Enviar PCM directamente al I2S
    // --------------------------------------------------------

    size_t bytesWritten = 0;

    esp_err_t err = i2s_channel_write(
        i2sTxHandle,
        audioBuffer,
        bytesRead,
        &bytesWritten,
        10
    );

    if (err != ESP_OK) {

        Serial.print("ERROR I2S: ");
        Serial.println((int)err);
    }
}
