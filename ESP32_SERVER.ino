#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <LittleFS.h>
#include <SPI.h>
#include <SD.h>

#include "animation.h"
#include "display_led.h"
#include "display_oled.h"
#include "config.h"
#include "index_html.h"

SPIClass sdSPI(FSPI);

// WiFi Access Point

const char* AP_SSID     = "ESP32_AP";
const char* AP_PASSWORD = "12345678";

IPAddress local_IP(192, 168, 4, 1);
IPAddress gateway(192, 168, 4, 1);
IPAddress subnet(255, 255, 255, 0);

// Servers

WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);

// File upload

File uploadFile;

String uploadFileName = "";
size_t uploadFileSize = 0;
bool uploadActive = false;

unsigned long lastClientCheck = 0;
const unsigned long CLIENT_CHECK_INTERVAL = 3000;

// WebSocket state

bool webSocketConnected = false;

void handleCommand(const String& msg, const String& source);
void listFiles(const String& path);
bool deleteFilePath(const String& path);
bool deleteDirectory(const String& path);
void sendFileList();
void sendWebSocketMessage(String msg);
void broadcastWebSocket(String msg);

// WebSocket messages

void sendWebSocketMessage(String msg) {
    if (webSocketConnected) {
        webSocket.broadcastTXT(msg);
    }
}

void broadcastWebSocket(String msg) {
    webSocket.broadcastTXT(msg);
}

// WebSocket callback

void webSocketEvent(
    uint8_t num,
    WStype_t type,
    uint8_t * payload,
    size_t length
) {

    switch (type) {
        case WStype_CONNECTED:

            webSocketConnected = true;
            //Serial.println("[WS] Client connected");

            webSocket.sendTXT(
                num,
                "[ESP32] WebSocket connected"
            );
            break;

        case WStype_DISCONNECTED:
            //Serial.println("[WS] Client disconnected");
            // There could still be another client.
            // Check the server state through the connection list
            webSocketConnected = false;
            break;

        case WStype_TEXT:
        {

            String msg;

            for (size_t i = 0; i < length; i++) {
                msg += (char)payload[i];
            }

            msg.trim();

            if (msg.length() == 0) {
                return;
            }

            //Serial.println("[WEB] Command: " + msg);

            handleCommand(
                msg,
                "Web"
            );

            break;

        }

        default:
            break;
    }
}


// ============================================================
// Command handling
// ============================================================

void handleCommand(
    const String& msg,
    const String& source
) {
    // FILES

    if (msg == "/files") {
        sendWebSocketMessage("[FS] BEGIN");
        listFiles("/");
        sendWebSocketMessage("[FS] END");
        return;
    }

    // MKDIR

    if (msg.startsWith("/mkdir ")) {

        String path = msg.substring(7);
        path.trim();

        if (!path.startsWith("/")) {
            path = "/" + path;
        }

        //Serial.println("[" + source + "] mkdir " + path);

        if (SD.mkdir(path)) {
            sendWebSocketMessage("[FS] MKDIR|" + path);
        }
        else {
            sendWebSocketMessage("[FS] MKDIR_ERROR|" + path);
        }

        return;
    }

    // RMDIR

    if (msg.startsWith("/rmdir ")) {
        String path = msg.substring(7);
        path.trim();

        if (!path.startsWith("/")) {
            path = "/" + path;
        }
        deleteDirectory(path);

        return;
    }

    // DELETE

    if (msg.startsWith("/delete ")) {

        String path = msg.substring(8);
        path.trim();

        if (!path.startsWith("/")) {
            path = "/" + path;
        }
        deleteFilePath(path);

        return;
    }

    // PLAY

    if (msg.startsWith("/play ")) {

        String path = msg.substring(6);
        path.trim();

        if (!path.startsWith("/")) {
            path = "/" + path;
        }

        //Serial.println("[" + source + "] play " + path);

        sendWebSocketMessage("[FS] PLAY|" + path);
        if (path.endsWith(".wav")) {
            startAudio(path);
        }
        else if (path.endsWith(".bin")) {
            startAnimation(path);
        }
        else {
            Serial.println("Formato no soportado: " + path);
        }

        return;
    }
    // HELP
    if (msg == "/help") {

        String help =
            "[ESP32] Commands: "
            "/ledon "
            "/ledoff "
            "/help "
            "/files "
            "/mkdir "
            "/rmdir "
            "/delete "
            "/play";

        //Serial.println(help);
        sendWebSocketMessage(help);

        return;
    }

    // Unknown command

    //Serial.println("[" + source + "] Unknown command: " + msg);


    sendWebSocketMessage(
        "[ESP32] Unknown command: " +
        msg
    );

}

// List files 

void listFiles(const String& path) {

    File root = SD.open(path);
    if (!root || !root.isDirectory()) {
        sendWebSocketMessage(
            "[FS] ERROR opening " +
            path
        );
        return;
    }

    File file = root.openNextFile();

    while (file) {
        String name = file.name();
        if (!name.startsWith("/")) {
            if (path == "/") name = "/" + name;
            else name = path + "/" + name;
        }

        if (file.isDirectory()) {
            sendWebSocketMessage("[FS] DIR|" + name);
            file.close();
            listFiles(name);
        }
        else {

            sendWebSocketMessage(
                "[FS] FILE|" +
                name +
                "|" +
                String(file.size())
            );
            file.close();
        }
        file = root.openNextFile();
    }
    root.close();
}


// ============================================================
// JSON file listing
// ============================================================

void addFilesRecursive(File dir, String currentPath, String &json, bool &first) {

    if (!dir || !dir.isDirectory()) return;
    File file = dir.openNextFile();

    while (file) {
        String fileName = file.name();
        int slash = fileName.lastIndexOf('/');
        if (slash >= 0) fileName = fileName.substring(slash + 1);

        // Construir la ruta completa
        String fullPath;

        if (currentPath == "/") fullPath = "/" + fileName;
        else fullPath = currentPath + "/" + fileName;

        if (!first) json += ",";

        first = false;

        json += "{";
        json += "\"path\":\"" + fullPath + "\",";
        json += "\"size\":" + String(file.size()) + ",";
        json += "\"directory\":";
        json += file.isDirectory() ? "true" : "false";
        json += "}";

        // Entrar en el directorio
        if (file.isDirectory()) {
            addFilesRecursive(
                file,
                fullPath,
                json,
                first
            );
        }

        file.close();
        file = dir.openNextFile();
    }
}


void sendFileList() {
    String json = "[";
    File root = SD.open("/");

    if (!root || !root.isDirectory()) {
        if (root) root.close();
        server.send(
            500,
            "application/json",
            "[]"
        );
        return;
    }

    bool first = true;
    addFilesRecursive(
        root,
        "/",
        json,
        first
    );

    root.close();
    json += "]";

    server.send(
        200,
        "application/json",
        json
    );
}

// Delete file

bool deleteFilePath(const String& path) {

    if (path.length() == 0 || path == "/") {
        sendWebSocketMessage("[FS] DELETE_ERROR|Invalid path");
        return false;
    }

    //Serial.println("[FS] Delete: " + path);

    if (!SD.exists(path)) {
        sendWebSocketMessage(
            "[FS] DELETE_ERROR|File not found|" +
            path
        );
        return false;
    }

    if (SD.remove(path)) {
        sendWebSocketMessage(
            "[FS] DELETED|" +
            path
        );
        return true;
    }

    sendWebSocketMessage(
        "[FS] DELETE_ERROR|Cannot delete|" +
        path
    );

    return false;
}

// Delete directory recursively

bool deleteDirectory(const String& path) {

    if (path.length() == 0 || path == "/") {
        sendWebSocketMessage("[FS] RMDIR_ERROR|Invalid path");
        return false;
    }


    File dir = SD.open(path);

    if (!dir || !dir.isDirectory()) {
        sendWebSocketMessage(
            "[FS] RMDIR_ERROR|Directory not found|" +
            path
        );
        return false;
    }

    File file = dir.openNextFile();


    while (file) {
        String name = file.name();
        String fullPath;

        if (name.startsWith("/")) fullPath = name;
        else fullPath = path + "/" + name;

        bool isDir = file.isDirectory();
        file.close();

        if (isDir) deleteDirectory(fullPath);
        else SD.remove(fullPath);

        file =dir.openNextFile();
    }

    dir.close();

    if (SD.rmdir(path)) {
        sendWebSocketMessage(
            "[FS] RMDIR|" +
            path
        );
        return true;
    }

    sendWebSocketMessage(
        "[FS] RMDIR_ERROR|Cannot remove|" +
        path
    );

    return false;
}

// HTTP: Main page

void handleRoot() {
    server.send_P(
        200,
        "text/html",
        index_html
    );
}


// HTTP: File upload

void handleUpload() {
    HTTPUpload& upload = server.upload();

    if (upload.status == UPLOAD_FILE_START) {
        uploadFileName = upload.filename;

        if (!uploadFileName.startsWith("/")) {
            uploadFileName = "/" + uploadFileName;
        }

        //Serial.println();
        //Serial.println("================================");

        //Serial.println("[UPLOAD] Start: " +uploadFileName);

        if (uploadFile) uploadFile.close();

        uploadFile =
            SD.open(
                uploadFileName,
                "w"
            );

        if (!uploadFile) {
            //Serial.println("[UPLOAD] ERROR: cannot open file");
            uploadActive = false;

            return;
        }

        uploadFileSize = 0;
        uploadActive = true;

        broadcastWebSocket(
            "[FILE] UPLOAD_START|" +
            uploadFileName
        );

    }
    else if (upload.status == UPLOAD_FILE_WRITE) {
        if (uploadActive &&uploadFile) {
            size_t written =
                uploadFile.write(
                    upload.buf,
                    upload.currentSize
                );

            uploadFileSize += written;

            if (written != upload.currentSize) {
                //Serial.println("[UPLOAD] ERROR: write failed");
                uploadFile.close();
                uploadActive = false;
            }
        }
    }
    else if (upload.status == UPLOAD_FILE_END) {

        if (uploadActive && uploadFile) uploadFile.close();
        uploadActive = false;

        //Serial.println("[UPLOAD] Finished: " + uploadFileName);
        //Serial.println("[UPLOAD] Size: " + String(uploadFileSize));
        //Serial.println("================================");

        broadcastWebSocket(
            "[FILE] UPLOAD_COMPLETE|" +
            uploadFileName +
            "|" +
            String(uploadFileSize)
        );

    }
    else if (upload.status == UPLOAD_FILE_ABORTED) {

        //Serial.println("[UPLOAD] ABORTED");

        if (uploadFile) uploadFile.close();
        uploadActive = false;

        if (
            uploadFileName.length() > 0 &&
            SD.exists(uploadFileName)
        ) SD.remove(uploadFileName);

        broadcastWebSocket(
            "[FILE] UPLOAD_ABORTED"
        );
    }
}

// HTTP: Upload result

void handleUploadFinished() {
    if (uploadActive) {
        server.send(
            500,
            "text/plain",
            "[UPLOAD] ERROR"
        );
        return;
    }

    server.send(
        200,
        "text/plain",
        "[UPLOAD] COMPLETE " +
        uploadFileName
    );

}


// HTTP: JSON file list
void handleApiFiles() {
    sendFileList();
}

// HTTP: Delete

void handleApiDelete() {
    if (!server.hasArg("path")) {
        server.send(
            400,
            "text/plain",
            "Missing path"
        );
        return;
    }

    String path = server.arg("path");

    if (!path.startsWith("/")) path = "/" + path;

    bool result = deleteFilePath(path);

    if (result) {
        server.send(
            200,
            "text/plain",
            "[FS] DELETED|" + path
        );
    }
    else {
        server.send(
            400,
            "text/plain",
            "[FS] DELETE_ERROR|" + path
        );
    }
}

void handleApiRmdir() {
    if (!server.hasArg("path")) {
        server.send(
            400,
            "text/plain",
            "Missing path"
        );
        return;
    }

    String path = server.arg("path");

    if (!path.startsWith("/")) path = "/" + path;

    bool result = deleteDirectory(path);

    if (result) {
        server.send(
            200,
            "text/plain",
            "[FS] DELETED|" + path
        );
    }
    else {
        server.send(
            400,
            "text/plain",
            "[FS] DELETE_ERROR|" + path
        );
    }
}

// HTTP: Make directory

void handleApiMkdir() {

    if (!server.hasArg("path")) {
        server.send(
            400,
            "text/plain",
            "Missing path"
        );
        return;
    }

    String path = server.arg("path");
    path.trim();

    if (!path.startsWith("/")) path = "/" + path;

    if (SD.mkdir(path)) {
        sendWebSocketMessage("[FS] MKDIR|" + path);
        server.send(
            200,
            "text/plain",
            "[FS] MKDIR|" + path
        );
    }
    else {
        server.send(
            400,
            "text/plain",
            "[FS] MKDIR_ERROR|" + path
        );
    }
}


// HTTP 404

void handleNotFound() {
    server.send(
        404,
        "text/plain",
        "Not found"
    );
}


// WiFi setup

void setupWiFi() {

    //Serial.println("[WiFi] Configuring AP...");

    if (
        !WiFi.softAPConfig(
            local_IP,
            gateway,
            subnet
        )
    ) {
        //Serial.println("[WiFi] AP configuration failed");
    }


    bool result =
        WiFi.softAP(
            AP_SSID,
            AP_PASSWORD,
            6,
            0,
            4
        );

    if (!result) {
        //Serial.println("[WiFi] Failed to start AP!");
        return;
    }

    //Serial.println();
    //Serial.println("========================================");
    //Serial.println("             ESP32 WiFi AP");
    //Serial.println("========================================");

    //Serial.print("SSID     : ");
    //Serial.println(AP_SSID);

    //Serial.print("Password : ");
    //Serial.println(AP_PASSWORD);

    //Serial.print("Channel  : ");
    //Serial.println(WiFi.channel());


    //Serial.print("AP MAC   : ");
    //Serial.println(WiFi.softAPmacAddress());

    //Serial.print("IP       : ");
    //Serial.println(WiFi.softAPIP());

    //Serial.println("========================================");

}

// HTTP server setup

void setupHTTPServer() {

    server.on(
        "/",
        HTTP_GET,
        handleRoot
    );

    server.on(
        "/api/files",
        HTTP_GET,
        handleApiFiles
    );

    server.on(
        "/api/delete",
        HTTP_POST,
        handleApiDelete
    );

    server.on(
        "/api/rmdir",
        HTTP_POST,
        handleApiRmdir
    );

    server.on(
        "/api/mkdir",
        HTTP_POST,
        handleApiMkdir
    );


    // IMPORTANT:
    // Upload data is processed by handleUpload().
    server.on(
        "/upload",
        HTTP_POST,
        handleUploadFinished,
        handleUpload
    );

    server.onNotFound(
        handleNotFound
    );

    server.begin();

    //Serial.println("[HTTP] Server started on port 80");
}

void setup() {

    Serial.begin(115200);

    //delay(3000);

    //Serial.println();
    //Serial.println("========================================");
    //Serial.println("       ESP32 WiFi Controller");
    //Serial.println("========================================");

    // Displays

    displayLedInit();
    //displayOledInit();
    
    SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);

    if (!SD.begin(SD_CS, SPI, 1000000)) {
        //Serial.println("ERROR: no se ha podido inicializar la SD");
        return;
    }
    initAudio();    

    //Serial.println("SD OK");

    setupWiFi();
    setupHTTPServer();

    webSocket.begin();
    webSocket.onEvent(webSocketEvent);

    //Serial.println("[WS] WebSocket server started on port 81");

    // Information

    //Serial.println();
    //Serial.println("Connect your device to:");

    //Serial.print("    SSID: ");
    //Serial.println(AP_SSID);

    //Serial.println("Then open:");
    //Serial.println("    http://192.168.4.1");
    //Serial.println();
}

void loop() {

    server.handleClient();
    webSocket.loop();
    updateAnimation();
    updateAudio();
    /*
    if (Serial.available()) {

        String msg =
            Serial.readStringUntil('\n');

        msg.trim();

        if (msg.length() > 0) {

            handleCommand(
                msg,
                "Local"
            );
        }

    }
    */

    unsigned long now = millis();

    delay(2);
}