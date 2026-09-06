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

#include "animation.h"
#include "display_led.h"
#include "display_oled.h"
#include "config.h"

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

// ==================== BLE State ====================
BLEServer *pServer = nullptr;
BLECharacteristic *pTxCharacteristic = nullptr;
bool deviceConnected = false;

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
      Serial.println("[FS] Deleted: " + path);
      sendViaBLE("[FS] DELETED|" + path);

    } 
    else {
      Serial.println("[FS] Delete failed: " + path);
      sendViaBLE("[FS] DELETE_ERROR|Cannot delete|" + path);
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

  displayLedInit();
  displayOledInit();

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  if (!LittleFS.begin(true)) {
    Serial.println("[LittleFS] ERROR mounting filesystem");
  } 
  else Serial.println("[LittleFS] Filesystem mounted successfully");
    
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
