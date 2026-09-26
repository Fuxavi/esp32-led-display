#include "animation.h"
#include "display_oled.h"
#include "display_led.h"
#include "config.h"
#include <driver/i2s.h>

#define I2S_PORT I2S_NUM_0

bool i2sInitialized = false;

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
#define DMA_BUF_COUNT 8
#define DMA_BUF_LEN   256

bool initAudio()
{
    // --------------------------------------------------------
    // Si ya está inicializado
    // --------------------------------------------------------

    if (i2sInitialized) {
        return true;
    }

    // --------------------------------------------------------
    // Configuración I2S legacy
    // --------------------------------------------------------

    i2s_config_t cfg = {
        .mode =
            (i2s_mode_t)(
                I2S_MODE_MASTER |
                I2S_MODE_TX
            ),

        .sample_rate =
            AUDIO_SAMPLE_RATE,

        .bits_per_sample =
            I2S_BITS_PER_SAMPLE_16BIT,

        // WAV estéreo: L R L R...
        .channel_format =
            I2S_CHANNEL_FMT_RIGHT_LEFT,

        .communication_format =
            I2S_COMM_FORMAT_I2S,

        .intr_alloc_flags =
            ESP_INTR_FLAG_LEVEL1,

        .dma_buf_count =
            DMA_BUF_COUNT,

        .dma_buf_len =
            DMA_BUF_LEN,

        .use_apll =
            true,

        .tx_desc_auto_clear =
            false,

        .fixed_mclk =
            0
    };

    // --------------------------------------------------------
    // Pines I2S
    // --------------------------------------------------------

    i2s_pin_config_t pins = {
        .bck_io_num =
            (gpio_num_t)I2S_BCLK,

        .ws_io_num =
            (gpio_num_t)I2S_LRC,

        .data_out_num =
            (gpio_num_t)I2S_DOUT,

        .data_in_num =
            I2S_PIN_NO_CHANGE
    };

    // --------------------------------------------------------
    // Instalar driver
    // --------------------------------------------------------

    esp_err_t err = i2s_driver_install(
        I2S_PORT,
        &cfg,
        0,
        nullptr
    );

    if (err != ESP_OK) {

        Serial.print("[I2S] driver_install ERROR: ");
        Serial.println((int)err);

        return false;
    }

    // --------------------------------------------------------
    // Configurar pines
    // --------------------------------------------------------

    err = i2s_set_pin(
        I2S_PORT,
        &pins
    );

    if (err != ESP_OK) {

        Serial.print("[I2S] set_pin ERROR: ");
        Serial.println((int)err);

        i2s_driver_uninstall(I2S_PORT);

        return false;
    }

    // --------------------------------------------------------
    // Configurar frecuencia / bits / canales
    // --------------------------------------------------------

    err = i2s_set_clk(
        I2S_PORT,
        AUDIO_SAMPLE_RATE,
        I2S_BITS_PER_SAMPLE_16BIT,
        I2S_CHANNEL_STEREO
    );

    if (err != ESP_OK) {

        Serial.print("[I2S] set_clk ERROR: ");
        Serial.println((int)err);

        i2s_driver_uninstall(I2S_PORT);

        return false;
    }

    // --------------------------------------------------------
    // Limpiar DMA
    // --------------------------------------------------------

    err = i2s_zero_dma_buffer(
        I2S_PORT
    );

    if (err != ESP_OK) {

        Serial.print("[I2S] zero_dma ERROR: ");
        Serial.println((int)err);

        i2s_driver_uninstall(I2S_PORT);

        return false;
    }

    // --------------------------------------------------------
    // Marcar como inicializado
    // --------------------------------------------------------

    i2sInitialized = true;


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

    // --------------------------------------------------------
    // Leer bloque de audio
    // --------------------------------------------------------

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
    // Asegurar que el bloque esté alineado con una muestra
    //
    // 16 bits estéreo = 4 bytes por frame
    // L (2 bytes) + R (2 bytes)
    // --------------------------------------------------------

    bytesRead -= bytesRead % 4;

    if (bytesRead == 0) {
        return;
    }

    // --------------------------------------------------------
    // Enviar PCM al I2S
    // --------------------------------------------------------

    size_t bytesWritten = 0;

    esp_err_t err = i2s_write(
        I2S_PORT,
        audioBuffer,
        bytesRead,
        &bytesWritten,
        portMAX_DELAY
    );

    if (err != ESP_OK) {

        Serial.print("[I2S] write ERROR: ");
        Serial.println((int)err);

        return;
    }

    // --------------------------------------------------------
    // Comprobar escritura parcial
    // --------------------------------------------------------

    if (bytesWritten != bytesRead) {

        Serial.print("[I2S] escritura parcial: ");
        Serial.print(bytesWritten);
        Serial.print("/");
        Serial.println(bytesRead);
    }
}
