/*
 * ESP32 BLE Chat + LED Control (Web Bluetooth)
 * Note: Web Bluetooth API only supports BLE, NOT Classic Bluetooth SPP.
 *       So this sketch uses BLE, not BluetoothSerial.
 */

#include <LittleFS.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

File uploadFile;

bool fileTransferActive = false;

uint32_t expectedFileSize = 0;
uint32_t receivedFileSize = 0;
uint32_t expectedChunk = 0;

String currentFileName = "";

// ==================== BLE UART Service UUIDs (Nordic UART Service) ====================
#define SERVICE_UUID           "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define CHARACTERISTIC_UUID_RX "6e400003-b5a3-f393-e0a9-e50e24dcca9e"  // Write  (Web -> ESP32)
#define CHARACTERISTIC_UUID_TX "6e400002-b5a3-f393-e0a9-e50e24dcca9e"  // Notify (ESP32 -> Web)

// ==================== LED ====================
#define LED_PIN 2              // Onboard LED, controlled by /ledon and /ledoff commands

// ==================== BLE State ====================
BLEServer *pServer = nullptr;
BLECharacteristic *pTxCharacteristic = nullptr;
bool deviceConnected = false;

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// Define OLED screen resolution
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

// Define OLED I2C address (default is usually 0x3C or 0x3D)
#define OLED_ADDR   0x3C

// Create OLED object
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ============================================================
// Variables de la animación
// ============================================================

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

// ============================================================
// Iniciar animación
// ============================================================

void startAnimation(String filename) {
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
 
    animationFrameSize = ((animationWidth * animationHeight) + 7) / 8;

    // Comprobar tamaño del archivo

    size_t expectedSize =
        8 + animationFrameSize * animationNumFrames;

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

    showBufferScaled(
        animationBuffer,
        animationWidth,
        animationHeight
    );
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

    showBufferScaled(
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

void showImage(String filename) {
    File file = LittleFS.open(filename, "r");

    if (!file) {
        Serial.println("No se pudo abrir la imagen");
        return;
    }

    // Comprobar que al menos contiene la cabecera
    uint16_t width;
    uint16_t height;

    if (file.read((uint8_t*)&width, sizeof(width)) != sizeof(width) ||
        file.read((uint8_t*)&height, sizeof(height)) != sizeof(height)) {
        Serial.println("Error leyendo cabecera");
        file.close();
        return;
    }

    Serial.printf("Imagen: %dx%d\n", width, height);

    // Comprobar dimensiones
    if (width == 0 || height == 0) {
        Serial.println("Dimensiones inválidas");
        file.close();
        return;
    }

    // Un píxel = 1 bit
    size_t imageSize = ((width * height) + 7) / 8;

    // Comprobar que el archivo tiene los datos esperados
    if (file.size() != 4 + imageSize) {
        Serial.println("Tamaño de archivo incorrecto");
        file.close();
        return;
    }

    // Comprobar que cabe en la OLED
    if (width > SCREEN_WIDTH || height > SCREEN_HEIGHT) {
        Serial.println("La imagen es demasiado grande para la OLED");
        file.close();
        return;
    }

    uint8_t *buffer = new uint8_t[imageSize];

    if (buffer == nullptr) {
        Serial.println("No hay memoria suficiente");
        file.close();
        return;
    }

    if (file.read(buffer, imageSize) != imageSize) {
        Serial.println("Error leyendo imagen");
        delete[] buffer;
        file.close();
        return;
    }

    file.close();

    showBufferScaled(buffer, width, height);

    delete[] buffer;
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

// ==================== Helper: send a message to the web page via BLE notify ====================
void sendViaBLE(const String &msg) {
  if (deviceConnected && pTxCharacteristic != nullptr) {
    pTxCharacteristic->setValue((msg + "\n").c_str());
    pTxCharacteristic->notify();
    delay(10);  // Give the BLE stack time to send
  }
}

void handleFilePacket(uint8_t *data, size_t length) {

  if (length < 1) return;

  uint8_t type = data[0];

  // =========================================
  // FILE BEGIN
  // =========================================
  if (type == 0x01) {

    if (length < 6) {
      sendViaBLE("[FILE] ERROR: invalid BEGIN packet");
      return;
    }

    expectedFileSize =
        ((uint32_t)data[1]) |
        ((uint32_t)data[2] << 8) |
        ((uint32_t)data[3] << 16) |
        ((uint32_t)data[4] << 24);

    String fileName = "";

    for (size_t i = 5; i < length; i++) {
      fileName += (char)data[i];
    }

    fileName.trim();

    if (!fileName.startsWith("/")) {
      fileName = "/" + fileName;
    }

    // Cerrar transferencia anterior si existe
    if (uploadFile) {
      uploadFile.close();
    }

    currentFileName = fileName;

    // Crear/sobrescribir archivo
    uploadFile = LittleFS.open(currentFileName, "w");

    if (!uploadFile) {
      sendViaBLE("[FILE] ERROR: cannot create file");
      return;
    }

    receivedFileSize = 0;
    expectedChunk = 0;
    fileTransferActive = true;

    Serial.println();
    Serial.println("================================");
    Serial.println("[FILE] Upload started");
    Serial.println("[FILE] Name: " + currentFileName);
    Serial.println("[FILE] Size: " + String(expectedFileSize));
    Serial.println("================================");

    sendViaBLE("[FILE] READY");

    return;
  }


  // =========================================
  // FILE CHUNK
  // =========================================
  if (type == 0x02) {

    if (!fileTransferActive || !uploadFile) {
      sendViaBLE("[FILE] ERROR: no active transfer");
      return;
    }

    if (length < 5) {
      sendViaBLE("[FILE] ERROR: invalid chunk");
      return;
    }

    uint32_t chunkNumber =
        ((uint32_t)data[1]) |
        ((uint32_t)data[2] << 8) |
        ((uint32_t)data[3] << 16) |
        ((uint32_t)data[4] << 24);

    // Verificar que no falten chunks
    if (chunkNumber != expectedChunk) {

      Serial.println(
        "[FILE] ERROR: expected chunk " +
        String(expectedChunk) +
        ", received " +
        String(chunkNumber)
      );

      sendViaBLE(
        "[FILE] ERROR CHUNK " + String(expectedChunk)
      );
      return;
    }

    size_t dataLength = length - 5;

    size_t written = uploadFile.write(
      data + 5,
      dataLength
    );

    if (written != dataLength) {
      sendViaBLE("[FILE] ERROR: write failed");
      uploadFile.close();
      fileTransferActive = false;
      return;
    }

    receivedFileSize += written;
    expectedChunk++;

    Serial.printf(
      "[FILE] Chunk %lu received - total %lu / %lu\n",
      chunkNumber,
      receivedFileSize,
      expectedFileSize
    );

    // Confirmar chunk
    sendViaBLE(
      "[FILE] ACK " + String(chunkNumber)
    );

    return;
  }
  // =========================================
  // FILE END
  // =========================================
  if (type == 0x03) {

    if (!fileTransferActive) {
      sendViaBLE("[FILE] ERROR: no active transfer");
      return;
    }

    uploadFile.close();
    fileTransferActive = false;

    Serial.println();
    Serial.println("================================");
    Serial.println("[FILE] Upload finished");
    Serial.println("[FILE] Received: " + String(receivedFileSize));
    Serial.println("[FILE] Expected: " + String(expectedFileSize));
    Serial.println("================================");

    if (receivedFileSize == expectedFileSize) sendViaBLE("[FILE] COMPLETE " + currentFileName);
    else sendViaBLE("[FILE] ERROR: size mismatch");

    return;
  }
}

void deleteDirectory(String dir_path) {
  File root = LittleFS.open(dir_path);

  if (!root || !root.isDirectory()) {
    return;
  }
  File file = root.openNextFile();
  while (file) {
    String name = file.name();
    if (file.isDirectory()) {
      file.close();
      deleteDirectory(dir_path + name + "/");
    }
    else {
      file.close();
      LittleFS.remove(dir_path + name);
    }
    file = root.openNextFile();
  }
  root.close();
  LittleFS.rmdir(dir_path);
}

void listFiles(String dir_path) {

  File root = LittleFS.open(dir_path);

  if (!root || !root.isDirectory()) {
    sendViaBLE("[FS] ERROR");
    return;
  }

  // Formato:
  // [FS] DIR|nombre
  String message = "[FS] DIR|" + dir_path;
  sendViaBLE(message);

  File file = root.openNextFile();

  while (file) {
    String name = dir_path + file.name();
    if (file.isDirectory()) listFiles(name + "/");
    else {
      size_t size = file.size();
      // Formato:
      // [FS] FILE|nombre|tamaño
      message =
        "[FS] FILE|" +
        name +
        "|" +
        String(size);
      sendViaBLE(message);
    }

    file.close();
    file = root.openNextFile();
  }
  root.close();
}

bool deleteFile(String path) {
  if (path.length() == 0) {
      sendViaBLE("[FS] DELETE_ERROR|Empty path");
      return true;
    }

    if (path == "/") {
      sendViaBLE("[FS] DELETE_ERROR|Cannot delete root");
      return true;
    }

    Serial.println(
      "[FS] Delete requested: " + path
    );

    if (!LittleFS.exists(path)) {

      sendViaBLE(
        "[FS] DELETE_ERROR|File not found|" + path
      );

      return true;
    }

    if (LittleFS.remove(path)) {

      Serial.println(
        "[FS] Deleted: " + path
      );

      sendViaBLE(
        "[FS] DELETED|" + path
      );

    } else {

      Serial.println(
        "[FS] Delete failed: " + path
      );

      sendViaBLE(
        "[FS] DELETE_ERROR|Cannot delete|" + path
      );
    }

    return true;
}

// ==================== Handle a command string, returns true if it was a command ====================
bool handleCommand(const String &msg, const String &source) {
  if (msg == "/ledon") {
    digitalWrite(LED_PIN, HIGH);
    Serial.println("[BLE] >>> " + source + " command: LED turned ON");
    sendViaBLE("[ESP32] LED turned ON");
    return true;
  }
  if (msg == "/ledoff") {
    digitalWrite(LED_PIN, LOW);
    Serial.println("[BLE] >>> " + source + " command: LED turned OFF");
    sendViaBLE("[ESP32] LED turned OFF");
    return true;
  }
  if (msg == "/files") {
    Serial.println("[FS] Listing files...");
    sendViaBLE("[FS] BEGIN");
    listFiles("/");
    sendViaBLE("[FS] END");
    return true;
  }
  if (msg.startsWith("/mkdir ")) {
    String path = msg.substring(7);
    path.trim();
    sendViaBLE("[FS] MKDIR|"+path);
    Serial.println("[BLE] >>> " + source + " command: mkdir"+ path);

    LittleFS.mkdir(path);
    return true;
  }
  if (msg.startsWith("/rmdir ")) {
    String path = msg.substring(7);
    path.trim();
    sendViaBLE("[FS] RMDIR|"+path);
    Serial.println("[BLE] >>> " + source + " command: rmdir"+ path);

    deleteDirectory(path);
    return true;
  }
  if (msg.startsWith("/delete ")) {
    String path = msg.substring(8);
    path.trim();
    sendViaBLE("[FS] DELETE|"+path);
    Serial.println("[BLE] >>> " + source + " command: delete"+ path);
    return deleteFile(path);
  }
  if (msg.startsWith("/show ")) {
    String path = msg.substring(6);
    path.trim();
    sendViaBLE("[FS] SHOW|"+path);
    Serial.println("[BLE] >>> " + source + " command: show"+ path);
    showImage(path);
  }
  if (msg.startsWith("/play ")) {
    String path = msg.substring(6);
    path.trim();
    sendViaBLE("[FS] PLAY|"+path);
    Serial.println("[BLE] >>> " + source + " command: play"+ path);
    startAnimation(path);
  }
  if (msg == "/help") {
    String help = "[ESP32] Commands: /ledon  /ledoff  /help  /files  /mkdir  /rmdir  /delete  /play";
    Serial.println("[BLE] >>> " + source + " command: /help");
    sendViaBLE(help);
    return true;
  }
  return false;
}

// ==================== BLE Server callbacks (connect / disconnect) ====================
class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *pServer) {
    deviceConnected = true;
    Serial.println("[BLE] >>> Web client connected!");
  }

  void onDisconnect(BLEServer *pServer) {
    deviceConnected = false;
    Serial.println("[BLE] <<< Web client disconnected. Restarting advertising...");
    BLEDevice::startAdvertising();
  }
};

// ==================== RX Characteristic callbacks (data arriving from web page) ====================
class RxCallbacks : public BLECharacteristicCallbacks {

  void onWrite(BLECharacteristic *pCharacteristic) {

    String rxValue = pCharacteristic->getValue();

    if (rxValue.length() == 0) {
      return;
    }

    uint8_t *data = (uint8_t *)rxValue.c_str();

    size_t length = rxValue.length();

    // =====================================
    // Paquetes de archivo
    // =====================================

    if (
      data[0] == 0x01 ||
      data[0] == 0x02 ||
      data[0] == 0x03
    ) {

      handleFilePacket(data, length);

      return;
    }

    // =====================================
    // Mensajes de texto
    // =====================================

    String msg = rxValue;

    msg.trim();

    if (msg.length() == 0) {
      return;
    }

    if (!handleCommand(msg, "Web")) {

      Serial.print("[BLE] Received: ");
      Serial.println(msg);

    }
  }
};

// ==================== setup ====================
void setup() {
  Serial.begin(115200);
  delay(1000);

  Wire.begin(21,22);
  // Initialize OLED screen
  if(!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;); // Infinite loop to terminate program
  }

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  if (!LittleFS.begin(true)) {

      Serial.println(
        "[LittleFS] ERROR mounting filesystem"
      );

    } else {

      Serial.println(
        "[LittleFS] Filesystem mounted successfully"
      );
    }

  Serial.println();
  Serial.println("========================================");
  Serial.println("  ESP32 BLE Chat — Starting up...");
  Serial.println("========================================");

  // Initialize BLE device
  BLEDevice::init("ESP32_BLE_Chat");

  // Request a larger MTU so longer messages fit in one packet
  BLEDevice::setMTU(512);

  // Create BLE server
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  // Create the Nordic UART-like service
  BLEService *pService = pServer->createService(SERVICE_UUID);

  // TX characteristic (notify) — ESP32 sends data to the web page
  pTxCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID_TX,
    BLECharacteristic::PROPERTY_NOTIFY
  );
  pTxCharacteristic->addDescriptor(new BLE2902());

  // RX characteristic (write) — web page sends data to ESP32
  BLECharacteristic *pRxCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID_RX,
    BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR
  );
  pRxCharacteristic->setCallbacks(new RxCallbacks());

  // Start the service
  pService->start();

  // Start advertising so the web page can discover and connect
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);  // Helps with iPhone discovery
  pAdvertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();

  Serial.println("[BLE] BLE device name: ESP32_BLE_Chat");
  Serial.println("[BLE] Advertising started. Open the web page to connect.");
  Serial.println();
  Serial.println("[BLE] Type a message and press Enter to send to the web page.");
  Serial.println("[BLE] Commands: /ledon  /ledoff  /help");
  Serial.println();
}

// ==================== loop ====================
void loop() {
  updateAnimation();
  // Read serial input: handle local commands or send to web page
  if (Serial.available()) {
    String msg = Serial.readStringUntil('\n');
    msg.trim();
    if (msg.length() > 0) {
      // Local commands work even without a web connection
      if (!handleCommand(msg, "Local")) {
        // Not a command -> send as a chat message to the web page
        if (deviceConnected) {
          sendViaBLE(msg);
          Serial.print("[BLE] Sent: ");
          Serial.println(msg);
        } else {
          Serial.println("[BLE] Not connected. Open the web page first.");
        }
      }
    }
  }

  delay(10);
}
