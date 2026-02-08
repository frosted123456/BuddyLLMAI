/**
 * ═══════════════════════════════════════════════════════════════════════════════
 * BUDDY ESP32 WiFi BRIDGE — Package 1
 * ═══════════════════════════════════════════════════════════════════════════════
 *
 * Replaces local face detection with a WiFi bridge architecture:
 *   - MJPEG stream on /stream for PC vision pipeline
 *   - WebSocket server on port 81 for AI commands (PC ↔ Teensy)
 *   - UDP listener on port 8888 for face data (PC → Teensy)
 *   - UART bridge to Teensy (921600 baud)
 *
 * Fixes included:
 *   - Phase 1E: UART mutex prevents interleaving of UDP face data and WS commands
 *   - Phase 1F: Dual-core architecture — HTTP/stream on core 0, WS+UDP on core 1
 *   - Phase 1G: 1024-byte Teensy RX buffer for large QUERY responses
 *
 * Board: ESP32-S3 (Freenove ESP32-S3 WROOM CAM)
 * Camera: OV2640/OV3660
 *
 * Tools Menu Settings:
 *   Board:              "ESP32S3 Dev Module"
 *   USB CDC On Boot:    "Enabled"
 *   CPU Frequency:      "240MHz (WiFi)"
 *   PSRAM:              "OPI PSRAM"
 *   Partition Scheme:   "Huge APP (3MB No OTA/1MB SPIFFS)"
 *   Flash Size:         "16MB (128Mb)"
 *
 * Required Libraries:
 *   - WebSocketsServer (by Markus Sattler) — Arduino Library Manager
 *   - ESP32 Arduino Core 2.0.14+
 */

#include "esp_camera.h"
#include <WiFi.h>
#include <WebServer.h>
#include <WiFiUdp.h>
#include <WebSocketsServer.h>
#include "esp_task_wdt.h"
#include "esp_system.h"          // For esp_reset_reason()
#include "esp32-hal-psram.h"     // For ps_malloc()

// ════════════════════════════════════════════════════════════════
// CONFIGURATION
// ════════════════════════════════════════════════════════════════

// WiFi — EDIT BEFORE UPLOADING
const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// Camera pins (Freenove ESP32-S3 WROOM)
#define PWDN_GPIO    -1
#define RESET_GPIO   -1
#define XCLK_GPIO    15
#define SIOD_GPIO     4
#define SIOC_GPIO     5
#define Y9_GPIO      16
#define Y8_GPIO      17
#define Y7_GPIO      18
#define Y6_GPIO      12
#define Y5_GPIO      10
#define Y4_GPIO       8
#define Y3_GPIO       9
#define Y2_GPIO      11
#define VSYNC_GPIO    6
#define HREF_GPIO     7
#define PCLK_GPIO    13

// Teensy UART
#define TEENSY_TX_PIN 43
#define TEENSY_RX_PIN 44
#define TEENSY_BAUD   921600
HardwareSerial TeensySerial(1);

// Network
#define HTTP_PORT     80
#define WS_PORT       81
#define UDP_PORT      8888

// Timing
#define TARGET_FPS         15
#define WDT_TIMEOUT_S      15
#define TEENSY_RESPONSE_TIMEOUT_MS 200
#define WIFI_RECONNECT_INTERVAL_MS 10000

// MJPEG boundary
#define MJPEG_BOUNDARY "buddyframe"

// ════════════════════════════════════════════════════════════════
// GLOBAL OBJECTS
// ════════════════════════════════════════════════════════════════

WebServer httpServer(HTTP_PORT);
WebSocketsServer wsServer(WS_PORT);
WiFiUDP udp;

// Phase 1E: UART mutex — prevents interleaving of UDP face data and WS commands
SemaphoreHandle_t uartMutex;

// Phase 1F: Frame mutex for shared camera frame
SemaphoreHandle_t frameMutex;
camera_fb_t* latestFrame = nullptr;

// Phase 1G: Teensy RX buffer (1024 bytes for large QUERY responses)
char teensyRxBuffer[1024];
int teensyRxPos = 0;

// UDP receive buffer
char udpBuffer[256];

// Statistics
volatile unsigned long udpReceived = 0;
volatile unsigned long wsMessagesIn = 0;
volatile unsigned long wsMessagesOut = 0;
volatile unsigned long framesSent = 0;
volatile unsigned long uartDropped = 0;

bool wifiConnected = false;

// Camera pause flag (for WiFi reconnection)
volatile bool cameraPaused = false;

// ════════════════════════════════════════════════════════════════
// CAMERA INITIALIZATION
// ════════════════════════════════════════════════════════════════

bool initCamera() {
    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer   = LEDC_TIMER_0;
    config.pin_d0       = Y2_GPIO;
    config.pin_d1       = Y3_GPIO;
    config.pin_d2       = Y4_GPIO;
    config.pin_d3       = Y5_GPIO;
    config.pin_d4       = Y6_GPIO;
    config.pin_d5       = Y7_GPIO;
    config.pin_d6       = Y8_GPIO;
    config.pin_d7       = Y9_GPIO;
    config.pin_xclk     = XCLK_GPIO;
    config.pin_pclk     = PCLK_GPIO;
    config.pin_vsync    = VSYNC_GPIO;
    config.pin_href     = HREF_GPIO;
    config.pin_sccb_sda = SIOD_GPIO;
    config.pin_sccb_scl = SIOC_GPIO;
    config.pin_pwdn     = PWDN_GPIO;
    config.pin_reset    = RESET_GPIO;
    config.xclk_freq_hz = 10000000;  // 10MHz — reduces EMI near WiFi antenna
    config.pixel_format = PIXFORMAT_JPEG;
    config.frame_size   = FRAMESIZE_VGA;     // 640x480
    config.jpeg_quality = 12;
    config.fb_count     = 2;
    config.fb_location  = CAMERA_FB_IN_PSRAM;
    config.grab_mode    = CAMERA_GRAB_LATEST;

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("[CAM] Init failed: 0x%x\n", err);
        return false;
    }
    Serial.println("[CAM] Initialized");
    return true;
}

// ════════════════════════════════════════════════════════════════
// WiFi
// ════════════════════════════════════════════════════════════════

bool setupWiFi() {
    Serial.printf("[WIFI] Connecting to %s...\n", WIFI_SSID);

    // Clean slate — kill any residual WiFi state
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(200);

    WiFi.mode(WIFI_STA);
    WiFi.persistent(false);          // Don't read/write NVS — avoids flash corruption issues
    WiFi.setAutoReconnect(false);    // We handle reconnection manually in checkWiFi()
    WiFi.setSleep(false);            // Disable power save BEFORE connecting (not after)
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - start > 20000) {
            Serial.println("[WIFI] Connection timeout");
            Serial.printf("[WIFI] Status code: %d\n", WiFi.status());
            return false;
        }
        delay(500);
        Serial.print(".");
    }
    Serial.printf("\n[WIFI] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("[WIFI] RSSI: %d dBm\n", WiFi.RSSI());
    return true;
}

// ════════════════════════════════════════════════════════════════
// Phase 1F: CAPTURE TASK — Runs on core 0
// Continuously captures frames and stores the latest
// ════════════════════════════════════════════════════════════════

void captureTask(void* param) {
    esp_task_wdt_add(NULL);  // Register this task with watchdog (WARN-4)
    unsigned long frameInterval = 1000 / TARGET_FPS;
    while (true) {
        esp_task_wdt_reset();  // Feed watchdog (WARN-4)

        // Pause capture during WiFi reconnection to free DMA
        if (cameraPaused) {
            delay(100);
            continue;
        }

        camera_fb_t* fb = esp_camera_fb_get();
        if (fb) {
            if (xSemaphoreTake(frameMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                if (latestFrame) esp_camera_fb_return(latestFrame);
                latestFrame = fb;
                xSemaphoreGive(frameMutex);
            } else {
                esp_camera_fb_return(fb);  // Couldn't get mutex, discard
            }
            framesSent++;
        }
        delay(frameInterval);
    }
}

// ════════════════════════════════════════════════════════════════
// HTTP HANDLERS
// ════════════════════════════════════════════════════════════════

void handleHealth() {
    char buf[256];
    snprintf(buf, sizeof(buf),
        "OK\nheap:%u\npsram:%u\nwifi:%d\nudp:%lu\nws_in:%lu\nws_out:%lu\nframes:%lu\nuptime:%lu",
        (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram(),
        WiFi.status() == WL_CONNECTED ? 1 : 0,
        udpReceived, wsMessagesIn, wsMessagesOut, framesSent, millis() / 1000);
    httpServer.send(200, "text/plain", buf);
}

void handleCapture() {
    esp_task_wdt_reset();

    // WARN-1 fix: Copy frame data before releasing mutex, then send from copy.
    // This prevents holding frameMutex during slow network I/O.
    if (xSemaphoreTake(frameMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (latestFrame != nullptr && latestFrame->len > 0) {
            // Copy frame data while holding mutex (fast memcpy)
            size_t len = latestFrame->len;
            uint8_t* copy = (uint8_t*)ps_malloc(len);  // Allocate from PSRAM (8MB available)
            if (copy) {
                memcpy(copy, latestFrame->buf, len);
                xSemaphoreGive(frameMutex);  // Release mutex BEFORE network IO

                // Send from copy (slow network IO, mutex released)
                httpServer.sendHeader("Access-Control-Allow-Origin", "*");
                httpServer.sendHeader("Cache-Control", "no-cache");
                httpServer.send_P(200, "image/jpeg", (const char*)copy, len);
                free(copy);
                return;
            }
        }
        xSemaphoreGive(frameMutex);
    }
    httpServer.send(503, "text/plain", "No frame available");
}

void handleStream() {
    WiFiClient client = httpServer.client();

    client.println("HTTP/1.1 200 OK");
    client.printf("Content-Type: multipart/x-mixed-replace; boundary=%s\r\n", MJPEG_BOUNDARY);
    client.println("Access-Control-Allow-Origin: *");
    client.println("Cache-Control: no-cache");
    client.println();

    unsigned long lastYield = millis();

    while (client.connected()) {
        esp_task_wdt_reset();

        if (xSemaphoreTake(frameMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            if (latestFrame != nullptr && latestFrame->len > 0) {
                // Copy frame data while holding mutex
                size_t len = latestFrame->len;
                uint8_t* copy = (uint8_t*)ps_malloc(len);
                if (copy) {
                    memcpy(copy, latestFrame->buf, len);
                    xSemaphoreGive(frameMutex);

                    // Send from copy (slow network IO, mutex released)
                    client.printf("--%s\r\n", MJPEG_BOUNDARY);
                    client.println("Content-Type: image/jpeg");
                    client.printf("Content-Length: %d\r\n", len);
                    client.println();
                    client.write(copy, len);
                    client.println();
                    free(copy);
                } else {
                    xSemaphoreGive(frameMutex);
                }
            } else {
                xSemaphoreGive(frameMutex);
            }
        }

        // Yield to other HTTP handlers periodically
        if (millis() - lastYield > 100) {
            lastYield = millis();
            delay(1);  // Allow httpServerTask to process other requests
        }

        delay(1000 / TARGET_FPS);
    }
}

void handleNotFound() {
    String msg = "Buddy ESP32 Bridge\n\n";
    msg += "Endpoints:\n";
    msg += "  GET /health   - Health check\n";
    msg += "  GET /capture  - Single JPEG frame\n";
    msg += "  GET /stream   - MJPEG stream\n";
    msg += "  WS  :81       - WebSocket command bridge\n";
    msg += "  UDP :8888     - Face data receiver\n";
    httpServer.send(404, "text/plain", msg);
}

// Phase 1F: HTTP server task runs on core 0 (never blocks core 1)
void httpServerTask(void* param) {
    esp_task_wdt_add(NULL);  // Register this task with watchdog (WARN-4)
    while (true) {
        esp_task_wdt_reset();  // Feed watchdog (WARN-4)
        httpServer.handleClient();
        delay(1);
    }
}

// ════════════════════════════════════════════════════════════════
// WebSocket — Command bridge (PC ↔ Teensy)
// ════════════════════════════════════════════════════════════════

void wsEvent(uint8_t clientNum, WStype_t type, uint8_t* payload, size_t length) {
    switch (type) {
        case WStype_DISCONNECTED:
            Serial.printf("[WS] Client %u disconnected\n", clientNum);
            break;

        case WStype_CONNECTED:
            Serial.printf("[WS] Client %u connected\n", clientNum);
            wsServer.sendTXT(clientNum, "{\"ok\":true,\"msg\":\"bridge_ready\"}");
            break;

        case WStype_TEXT: {
            wsMessagesIn++;
            char* cmd = (char*)payload;

            // Phase 1E: UART mutex prevents interleaving with UDP face data
            if (xSemaphoreTake(uartMutex, pdMS_TO_TICKS(300)) == pdTRUE) {
                // Clear UART RX buffer before sending command
                while (TeensySerial.available()) TeensySerial.read();

                TeensySerial.println(cmd);
                TeensySerial.flush();

                // Wait for response from Teensy (REC-4: fixed-size buffer)
                unsigned long waitStart = millis();
                char response[1024];
                int responseLen = 0;
                bool gotResponse = false;

                while (millis() - waitStart < TEENSY_RESPONSE_TIMEOUT_MS) {
                    while (TeensySerial.available()) {
                        char c = TeensySerial.read();
                        if (c == '\n') {
                            gotResponse = true;
                            break;
                        }
                        if (responseLen < (int)sizeof(response) - 1) {
                            response[responseLen++] = c;
                        }
                    }
                    if (gotResponse) break;
                    delayMicroseconds(100);
                }

                xSemaphoreGive(uartMutex);

                // Drain any stale face data that accumulated during mutex hold (REC-6)
                // This data arrived while we were waiting for the QUERY response
                // and is now old. Discard it so handleUDP processes fresh data next.
                while (TeensySerial.available()) {
                    TeensySerial.read();
                }

                if (gotResponse) {
                    // Trim trailing \r if present
                    while (responseLen > 0 && (response[responseLen - 1] == '\r' || response[responseLen - 1] == ' ')) {
                        responseLen--;
                    }
                    response[responseLen] = '\0';
                    wsServer.sendTXT(clientNum, response);
                } else {
                    wsServer.sendTXT(clientNum, "{\"ok\":false,\"reason\":\"timeout\"}");
                }
            } else {
                wsServer.sendTXT(clientNum, "{\"ok\":false,\"reason\":\"uart_busy\"}");
            }
            wsMessagesOut++;
            break;
        }

        default:
            break;
    }
}

// ════════════════════════════════════════════════════════════════
// UDP — Face data receiver (PC → ESP32 → Teensy)
// ════════════════════════════════════════════════════════════════

void handleUDP() {
    int packetSize = udp.parsePacket();
    if (packetSize == 0) return;

    int len = udp.read(udpBuffer, sizeof(udpBuffer) - 1);
    if (len <= 0) return;
    udpBuffer[len] = '\0';
    udpReceived++;

    // Phase 1E: UART mutex — if busy (command in progress), drop this frame.
    // Next face data arrives in ~33ms — acceptable to drop one.
    if (xSemaphoreTake(uartMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        TeensySerial.println(udpBuffer);
        xSemaphoreGive(uartMutex);
    } else {
        uartDropped++;
    }
}

// ════════════════════════════════════════════════════════════════
// Check for unsolicited Teensy messages (STATE: broadcasts)
// ════════════════════════════════════════════════════════════════

void checkTeensyUnsolicited() {
    // Read any unsolicited data from Teensy (e.g., STATE: broadcasts)
    while (TeensySerial.available()) {
        char c = TeensySerial.read();
        if (c == '\n' || teensyRxPos >= (int)(sizeof(teensyRxBuffer) - 2)) {
            // Phase 1G: Overflow protection
            if (teensyRxPos >= (int)(sizeof(teensyRxBuffer) - 2)) {
                teensyRxPos = 0;
                continue;
            }
            teensyRxBuffer[teensyRxPos] = '\0';
            teensyRxPos = 0;

            // Forward STATE broadcasts to connected WebSocket clients
            if (strncmp(teensyRxBuffer, "STATE:", 6) == 0) {
                wsServer.broadcastTXT(teensyRxBuffer);
            }
        } else {
            teensyRxBuffer[teensyRxPos++] = c;
        }
    }
}

// ════════════════════════════════════════════════════════════════
// WiFi reconnection check
// ════════════════════════════════════════════════════════════════

void checkWiFi() {
    static unsigned long lastCheck = 0;
    static int reconnectAttempts = 0;
    if (millis() - lastCheck < WIFI_RECONNECT_INTERVAL_MS) return;
    lastCheck = millis();

    if (WiFi.status() != WL_CONNECTED) {
        reconnectAttempts++;
        Serial.printf("[WIFI] Disconnected, reconnecting (attempt %d)...\n", reconnectAttempts);

        // Pause camera to free DMA for WiFi
        cameraPaused = true;
        delay(200);  // Let current frame capture complete

        // Full radio reset — disconnect alone sometimes isn't enough on ESP32-S3
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        delay(500);
        WiFi.mode(WIFI_STA);
        WiFi.persistent(false);
        WiFi.setAutoReconnect(false);
        WiFi.setSleep(false);
        delay(500);
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

        unsigned long start = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
            esp_task_wdt_reset();
            delay(250);
        }

        // Resume camera regardless of WiFi result
        cameraPaused = false;

        if (WiFi.status() == WL_CONNECTED) {
            Serial.printf("[WIFI] Reconnected: %s\n", WiFi.localIP().toString().c_str());
            Serial.printf("[WIFI] RSSI: %d dBm\n", WiFi.RSSI());
            WiFi.setSleep(false);
            reconnectAttempts = 0;

            // Start network services if they were deferred during setup
            startNetworkServices();
        } else if (reconnectAttempts >= 10) {
            Serial.println("[WIFI] Too many failures, rebooting...");
            delay(1000);
            ESP.restart();
        }
    } else {
        reconnectAttempts = 0;
    }
}

// ════════════════════════════════════════════════════════════════
// Network Service Startup — called once when WiFi first connects
// ════════════════════════════════════════════════════════════════

bool networkServicesStarted = false;

void startNetworkServices() {
    if (networkServicesStarted) return;  // Idempotent — safe to call multiple times

    httpServer.begin();
    Serial.printf("[HTTP] Server on port %d\n", HTTP_PORT);

    wsServer.begin();
    wsServer.onEvent(wsEvent);
    Serial.printf("[WS] Server on port %d\n", WS_PORT);

    udp.begin(UDP_PORT);
    Serial.printf("[UDP] Listening on port %d\n", UDP_PORT);

    networkServicesStarted = true;
    Serial.println("[NET] All network services active");
}

// ════════════════════════════════════════════════════════════════
// SETUP
// ════════════════════════════════════════════════════════════════

void setup() {
    Serial.begin(921600);
    delay(1000);

    Serial.println("\n╔════════════════════════════════════════╗");
    Serial.println("║  BUDDY ESP32 WiFi BRIDGE v1.1          ║");
    Serial.println("║  Package 1: WiFi ↔ UART Bridge         ║");
    Serial.println("╚════════════════════════════════════════╝\n");

    // ═══ DIAGNOSTIC: Check if last boot was a brownout reset ═══
    int resetReason = esp_reset_reason();
    Serial.printf("[BOOT] Reset reason: %d ", resetReason);
    switch (resetReason) {
        case 1:  Serial.println("(power-on)"); break;
        case 3:  Serial.println("(software)"); break;
        case 9:  Serial.println("(BROWNOUT — check power supply!)"); break;
        case 15: Serial.println("(watchdog)"); break;
        default: Serial.printf("(code %d)\n", resetReason); break;
    }

    // ═══ 1. WiFi FIRST — before anything uses DMA or PSRAM ═══
    // Camera DMA and PSRAM OPI compete with WiFi's internal DMA.
    // WiFi must establish connection before contention begins.
    wifiConnected = false;
    for (int attempt = 1; attempt <= 3; attempt++) {
        Serial.printf("[WIFI] Connection attempt %d/3...\n", attempt);
        if (setupWiFi()) {
            wifiConnected = true;
            break;
        }
        Serial.println("[WIFI] Failed, retrying after cooldown...");
        WiFi.disconnect(true);   // Force radio off between attempts
        delay(3000);             // Let voltage rails stabilize
    }

    if (!wifiConnected) {
        Serial.println("[WIFI] ⚠ All attempts failed — will retry in background");
    }

    // ═══ 2. Camera SECOND — after WiFi is established ═══
    int cameraRetries = 0;
    while (!initCamera()) {
        cameraRetries++;
        if (cameraRetries >= 5) {
            Serial.println("[FATAL] Camera init failed 5 times, rebooting...");
            delay(1000);
            ESP.restart();
        }
        Serial.printf("[CAM] Init failed, retry %d/5...\n", cameraRetries);
        delay(2000);
    }
    Serial.println("[CAM] Camera initialized successfully");

    // ═══ 3. UART THIRD — after WiFi + camera are stable ═══
    // 921600 baud lines near PCB antenna generate EMI.
    // Starting UART last minimizes interference with WiFi association.
    TeensySerial.setRxBufferSize(1024);
    TeensySerial.begin(TEENSY_BAUD, SERIAL_8N1, TEENSY_RX_PIN, TEENSY_TX_PIN);
    delay(100);
    TeensySerial.println("ESP32_READY");
    Serial.println("[UART] Teensy UART initialized");

    // ═══ 4. Create synchronization primitives ═══
    uartMutex = xSemaphoreCreateMutex();
    frameMutex = xSemaphoreCreateMutex();

    // ═══ 5. Register HTTP handlers (just function pointers, no network needed) ═══
    httpServer.on("/health", HTTP_GET, handleHealth);
    httpServer.on("/capture", HTTP_GET, handleCapture);
    httpServer.on("/stream", HTTP_GET, handleStream);
    httpServer.onNotFound(handleNotFound);

    // ═══ 6. Start network services ONLY if WiFi is connected ═══
    if (wifiConnected) {
        startNetworkServices();
    } else {
        Serial.println("[NET] Services deferred — will start when WiFi connects");
    }

    // ═══ 7. Initialize watchdog ═══
    esp_task_wdt_init(WDT_TIMEOUT_S, true);
    esp_task_wdt_add(NULL);

    // ═══ 8. Start background tasks ═══
    // Capture task on core 0 (shares with HTTP/stream)
    xTaskCreatePinnedToCore(captureTask, "capture", 8192, NULL, 1, NULL, 0);
    // HTTP server task on core 0 (handles blocking /stream)
    xTaskCreatePinnedToCore(httpServerTask, "httpd", 8192, NULL, 1, NULL, 0);

    Serial.println("\n[READY] Bridge active");
    if (wifiConnected) {
        Serial.printf("  Stream:  http://%s/stream\n", WiFi.localIP().toString().c_str());
        Serial.printf("  WS:      ws://%s:%d\n", WiFi.localIP().toString().c_str(), WS_PORT);
        Serial.printf("  UDP:     %s:%d\n", WiFi.localIP().toString().c_str(), UDP_PORT);
    } else {
        Serial.println("  ⚠ WiFi offline — will retry in background");
    }
    Serial.println();
}

// ════════════════════════════════════════════════════════════════
// MAIN LOOP — Runs on core 1
// Phase 1F: Only handles WebSocket + UDP (never blocked by stream)
// ════════════════════════════════════════════════════════════════

void loop() {
    esp_task_wdt_reset();

    // WebSocket processing
    wsServer.loop();

    // UDP face data forwarding
    handleUDP();

    // Check for unsolicited Teensy messages
    checkTeensyUnsolicited();

    // WiFi health check
    checkWiFi();

    // Periodic status logging
    static unsigned long lastStatusLog = 0;
    if (millis() - lastStatusLog > 60000) {
        lastStatusLog = millis();
        Serial.printf("[STATUS] UDP:%lu WS_in:%lu WS_out:%lu Frames:%lu Dropped:%lu Heap:%u\n",
            udpReceived, wsMessagesIn, wsMessagesOut, framesSent, uartDropped,
            (unsigned)ESP.getFreeHeap());
    }

    delay(1);
}
