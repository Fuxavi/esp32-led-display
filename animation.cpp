#include "animation.h"
#include "display_oled.h"
#include "display_led.h"
#include "config.h"
#include <Audio.h>

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

File testAudioFile;

bool testAudioPlaying = false;

uint8_t testAudioBuffer[512];

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


// ============================================================
// INICIAR AUDIO
// ============================================================

bool startAudio()
{
    // --------------------------------------------------------
    // Comprobar formato
    // --------------------------------------------------------

    if (audioSize == 0) {

        return false;
    }

    if (audioSampleRate != 16000) {

        return false;
    }

    if (audioChannels != 1) {

        return false;
    }

    if (audioBits != 16) {

        return false;
    }

    // --------------------------------------------------------
    // Inicializar I2S
    // --------------------------------------------------------

    if (!initAudio()) {

        return false;
    }

    // --------------------------------------------------------
    // Abrir de nuevo el mismo archivo
    //
    // Es importante que audioFile tenga su propio cursor.
    // animationFile seguirá leyendo los frames.
    // --------------------------------------------------------

    audioFile = SD.open(
        animationFile.name(),
        FILE_READ
    );

    if (!audioFile) {

        return false;
    }

    // --------------------------------------------------------
    // Ir al comienzo del audio
    // --------------------------------------------------------

    if (!audioFile.seek(audioOffset)) {

        audioFile.close();

        return false;
    }

    uint8_t debugBuffer[32];

    int debugRead = audioFile.read(debugBuffer, sizeof(debugBuffer));

    Serial.println("=== DEBUG AUDIO BIN ===");

    Serial.print("Posicion audioFile: ");
    Serial.println(audioFile.position());

    Serial.print("audioOffset: ");
    Serial.println(audioOffset);

    Serial.print("audioSize: ");
    Serial.println(audioSize);

    Serial.println("Primeros bytes:");

    for (int i = 0; i < debugRead; i++) {

        if (debugBuffer[i] < 16) {
            Serial.print("0");
        }

        Serial.print(debugBuffer[i], HEX);
        Serial.print(" ");
    }

    Serial.println();

    // Volver exactamente al comienzo del audio
    audioFile.seek(audioOffset);

    audioBytesPlayed = 0;
    audioPlaying = true;

    // --------------------------------------------------------
    // Estado inicial
    // --------------------------------------------------------

    audioBytesPlayed = 0;

    audioPlaying = true;

    return true;
}


// ============================================================
// ACTUALIZAR AUDIO
// ============================================================
//
// Esta función NO reproduce toda la canción.
//
// Cada llamada:
//
//     SD -> pequeño buffer -> I2S
//
// y vuelve inmediatamente al loop.
//
// ============================================================

void updateAudio()
{
    if (!audioPlaying) {
        return;
    }

    // --------------------------------------------------------
    // ¿Ha terminado?
    // --------------------------------------------------------

    if (audioBytesPlayed >= audioSize) {

        audioPlaying = false;

        audioFile.close();

        Serial.println("Audio BIN terminado");

        return;
    }

    // --------------------------------------------------------
    // Calcular cuántos bytes leer
    // --------------------------------------------------------

    size_t bytesToRead = AUDIO_BUFFER_SIZE;

    uint32_t bytesRemaining =
        audioSize - audioBytesPlayed;

    if (bytesRemaining < bytesToRead) {
        bytesToRead = bytesRemaining;
    }

    // PCM 16-bit = siempre muestras completas
    bytesToRead &= ~1;

    if (bytesToRead == 0) {

        audioPlaying = false;

        audioFile.close();

        return;
    }

    // --------------------------------------------------------
    // Leer PCM desde SD
    // --------------------------------------------------------

    int bytesRead = audioFile.read(
        audioBuffer,
        bytesToRead
    );

    if (bytesRead <= 0) {

        Serial.println("ERROR: no se pudo leer audio BIN");

        audioPlaying = false;

        audioFile.close();

        return;
    }

    // Mantener muestras completas de 16 bits
    bytesRead &= ~1;

    if (bytesRead == 0) {

        audioPlaying = false;

        audioFile.close();

        return;
    }

    // --------------------------------------------------------
    // Mandar PCM al I2S
    // --------------------------------------------------------

    size_t bytesWritten = 0;

    esp_err_t result = i2s_channel_write(
        i2sTxHandle,
        audioBuffer,
        bytesRead,
        &bytesWritten,
        10              // IMPORTANTE: dar tiempo al DMA
    );

    if (result != ESP_OK) {

        Serial.print("ERROR I2S BIN: ");
        Serial.println((int)result);

        // Hemos leído el bloque pero no se ha podido enviar.
        // Retrocedemos para intentar enviarlo de nuevo.
        if (bytesWritten < (size_t)bytesRead) {

            size_t bytesNotWritten =
                bytesRead - bytesWritten;

            audioFile.seek(
                audioFile.position() - bytesNotWritten
            );
        }

        return;
    }

    // --------------------------------------------------------
    // Actualizar contador
    // --------------------------------------------------------

    audioBytesPlayed += bytesWritten;

    // --------------------------------------------------------
    // Debug opcional
    // --------------------------------------------------------

    /*
    Serial.print("BIN audio: ");
    Serial.print(audioBytesPlayed);
    Serial.print(" / ");
    Serial.println(audioSize);
    */

    // --------------------------------------------------------
    // Si hemos terminado
    // --------------------------------------------------------

    if (audioBytesPlayed >= audioSize) {

        audioPlaying = false;

        audioFile.close();

        Serial.println("Audio BIN terminado");
    }
}


// ============================================================
// INICIAR ANIMACIÓN
// ============================================================

void startAnimation(
    String filename,
    bool ledDisplay
)
{
    // --------------------------------------------------------
    // Detener animación anterior
    // --------------------------------------------------------

    stopAnimation();

    // --------------------------------------------------------
    // Abrir archivo
    // --------------------------------------------------------

    animationFile =
        SD.open(
            filename,
            FILE_READ
        );

    if (!animationFile) {

        return;
    }

    // ========================================================
    // CABECERA
    // ========================================================
    //
    // 0  - uint16 width
    // 2  - uint16 height
    // 4  - uint16 numFrames
    // 6  - uint16 fps
    //
    // 8  - uint32 audioOffset
    // 12 - uint32 audioSize
    // 16 - uint32 sampleRate
    // 20 - uint32 audioFormat
    //
    // audioFormat:
    //
    // bits  0..15 = channels
    // bits 16..31 = bits per sample
    //
    // ========================================================

    animationFile.seek(0);

    if (animationFile.read((uint8_t*)&animationWidth, 2) != 2) {
        Serial.println("Error leyendo animationWidth");
        return;
    }

    if (animationFile.read((uint8_t*)&animationHeight, 2) != 2) {
        Serial.println("Error leyendo animationHeight");
        return;
    }

    if (animationFile.read((uint8_t*)&animationNumFrames, 2) != 2) {
        Serial.println("Error leyendo animationNumFrames");
        return;
    }

    if (animationFile.read((uint8_t*)&animationFPS, 2) != 2) {
        Serial.println("Error leyendo animationFPS");
        return;
    }

    if (animationFile.read((uint8_t*)&audioOffset, 4) != 4) {
        Serial.println("Error leyendo audioOffset");
        return;
    }

    if (animationFile.read((uint8_t*)&audioSize, 4) != 4) {
        Serial.println("Error leyendo audioSize");
        return;
    }

    if (animationFile.read((uint8_t*)&audioSampleRate, 4) != 4) {
        Serial.println("Error leyendo audioSampleRate");
        return;
    }

    if (animationFile.read((uint8_t*)&audioChannels, 2) != 2) {
        Serial.println("Error leyendo audioChannels");
        return;
    }

    if (animationFile.read((uint8_t*)&audioBits, 2) != 2) {
        Serial.println("Error leyendo audioBits");
        return;
    }


    // ========================================================
    // COMPROBACIONES
    // ========================================================

    if (animationNumFrames == 0) {

        stopAnimation();

        return;
    }

    if (animationWidth == 0 ||
        animationHeight == 0) {

        stopAnimation();

        return;
    }


    // ========================================================
    // TAMAÑO DE FRAME
    // ========================================================

    if (ledDisplay) {

        animationFrameSize =
            (size_t)animationWidth *
            animationHeight *
            2;

    } else {

        animationFrameSize =
            (
                (
                    (size_t)animationWidth *
                    animationHeight
                )
                + 7
            ) / 8;
    }


    // ========================================================
    // POSICIÓN DONDE TERMINAN LOS FRAMES
    // ========================================================

    uint32_t framesSize =
        (
            uint32_t
        )animationFrameSize *
        animationNumFrames;

    uint32_t framesEnd =
        24 +
        framesSize;


    // ========================================================
    // Comprobar audio
    // ========================================================

    if (audioSize > 0) {

        if (audioOffset < framesEnd) {

            stopAnimation();

            return;
        }

        if (
            (
                uint64_t
            )audioOffset +
            audioSize >
            animationFile.size()
        ) {

            stopAnimation();

            return;
        }

        if (audioSampleRate != 16000) {

            stopAnimation();

            return;
        }

        if (audioChannels != 1) {

            stopAnimation();

            return;
        }

        if (audioBits != 16) {

            stopAnimation();

            return;
        }
    }


    // ========================================================
    // Comprobar que los frames caben
    // ========================================================

    if (
        (
            uint64_t
        )framesEnd >
        animationFile.size()
    ) {

        stopAnimation();

        return;
    }


    // ========================================================
    // Reservar buffer de UN frame
    // ========================================================

    animationBuffer =
        new uint8_t[
            animationFrameSize
        ];

    if (animationBuffer == nullptr) {

        stopAnimation();

        return;
    }


    // ========================================================
    // FPS
    // ========================================================

    if (animationFPS > 0) {

        animationFrameDelay =
            1000 /
            animationFPS;

    } else {

        animationFrameDelay = 0;
    }


    // ========================================================
    // Estado inicial
    // ========================================================

    animationCurrentFrame = 0;

    animationLastFrameTime =
        millis();

    animationPlaying = true;

    usingLedDisplay =
        ledDisplay;


    // ========================================================
    // Ir al primer frame
    // ========================================================

    if (!animationFile.seek(24)) {

        stopAnimation();

        return;
    }


    // ========================================================
    // Leer primer frame
    // ========================================================

    if (
        animationFile.read(
            animationBuffer,
            animationFrameSize
        )
        != animationFrameSize
    ) {

        stopAnimation();

        return;
    }


    // ========================================================
    // Mostrar primer frame
    // ========================================================

    if (usingLedDisplay) {

        displayLedShowBufferScaled(
            animationBuffer,
            animationWidth,
            animationHeight
        );

    } else {

        displayOledShowBufferScaled(
            animationBuffer,
            animationWidth,
            animationHeight
        );
    }


    // ========================================================
    // Iniciar audio
    // ========================================================

    if (audioSize > 0) {

        startAudio();
    }
}


// ============================================================
// ACTUALIZAR ANIMACIÓN
// ============================================================

void updateAnimation()
{
    if (!animationPlaying) {
        return;
    }

    // ========================================================
    // AUDIO
    // ========================================================
    //
    // Se procesa en pequeños bloques.
    //
    // ========================================================

    // ========================================================
    // ANIMACIÓN
    // ========================================================

    uint32_t now = millis();

    // --------------------------------------------------------
    // Todavía no toca cambiar frame
    // --------------------------------------------------------

    if (
        animationFrameDelay > 0 &&
        now - animationLastFrameTime <
            animationFrameDelay
    ) {

        return;
    }


    // --------------------------------------------------------
    // Actualizar temporizador
    // --------------------------------------------------------

    animationLastFrameTime = now;


    // --------------------------------------------------------
    // Siguiente frame
    // --------------------------------------------------------

    animationCurrentFrame++;


    // ========================================================
    // FIN DE ANIMACIÓN -> LOOP
    // ========================================================

    if (
        animationCurrentFrame >=
        animationNumFrames
    ) {

        animationCurrentFrame = 0;

        // La nueva cabecera ocupa 24 bytes.
        animationFile.seek(24);
    }


    // ========================================================
    // Leer frame
    // ========================================================

    if (
        animationFile.read(
            animationBuffer,
            animationFrameSize
        )
        != animationFrameSize
    ) {

        stopAnimation();

        return;
    }


    // ========================================================
    // Mostrar frame
    // ========================================================

    if (usingLedDisplay) {

        displayLedShowBufferScaled(
            animationBuffer,
            animationWidth,
            animationHeight
        );

    } else {

        displayOledShowBufferScaled(
            animationBuffer,
            animationWidth,
            animationHeight
        );
    }
}


// ============================================================
// DETENER ANIMACIÓN
// ============================================================

void stopAnimation()
{
    animationPlaying = false;

    // --------------------------------------------------------
    // Parar audio
    // --------------------------------------------------------

    stopAudio();


    // --------------------------------------------------------
    // Cerrar archivo de animación
    // --------------------------------------------------------

    if (animationFile) {

        animationFile.close();
    }


    // --------------------------------------------------------
    // Liberar frame
    // --------------------------------------------------------

    if (animationBuffer != nullptr) {

        delete[] animationBuffer;

        animationBuffer = nullptr;
    }


    // --------------------------------------------------------
    // Reset
    // --------------------------------------------------------

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