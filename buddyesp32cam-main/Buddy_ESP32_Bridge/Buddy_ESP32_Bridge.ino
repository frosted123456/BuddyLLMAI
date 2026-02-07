/**
 * ═══════════════════════════════════════════════════════════════════════════════
 * BUDDY ESP32-S3 — WiFi Camera Bridge
 * ═══════════════════════════════════════════════════════════════════════════════
 *
 * Replaces onboard face detection with:
 *   1. MJPEG video stream over WiFi (for PC vision processing)
 *   2. WebSocket bridge: PC ↔ WiFi ↔ UART ↔ Teensy (commands/state)
 *   3. UDP fast path: PC face coordinates → UART → Teensy (tracking)
 *
 * The ESP32 no longer does any vision processing. It's a bridge.
 *
 * Board:    Freenove ESP32-S3 WROOM CAM
 * Camera:   OV2640/OV3660 (hardware detected)
 * PSRAM:    8MB OPI (required)
 *
 * ═══════════════════════════════════════════════════════════════════════════════
 * ARDUINO IDE SETTINGS — SAME AS PREVIOUS FIRMWARE
 * ═══════════════════════════════════════════════════════════════════════════════
 *
 * Board:              "ESP32S3 Dev Module"
 * USB CDC On Boot:    "Enabled"
 * CPU Frequency:      "240MHz (WiFi)"
 * PSRAM:              "OPI PSRAM"
 * Partition Scheme:   "Huge APP (3MB No OTA/1MB SPIFFS)"
 * Flash Size:         "16MB (128Mb)"
 * Upload Speed:       "921600"
 * USB Mode:           "Hardware CDC and JTAG"
 *
 * ═══════════════════════════════════════════════════════════════════════════════
 * REQUIRED LIBRARIES
 * ═══════════════════════════════════════════════════════════════════════════════
 *
 * Library Name                  | Version | Source
 * ------------------------------|---------|----------------------------------------
 * WebSockets (Markus Sattler)   | 2.4.0+  | Arduino Library Manager
 * ESP32 Arduino Core            | 2.0.14+ | Board Manager (includes WiFi, WebServer)
 *
 * Built-in (no install needed):
 *   - WiFi.h          (ESP32 core)
 *   - WebServer.h     (ESP32 core)
 *   - WiFiUdp.h       (ESP32 core)
 *   - esp_camera.h    (ESP32 core)
 *
 * Removed (no longer needed):
 *   - EloquentEsp32cam (was used for onboard face detection)
 *   - HistogramTracker.h (was used for histogram-based tracking fallback)
 *
 * ═══════════════════════════════════════════════════════════════════════════════
 * HARDWARE CONFIGURATION
 * ═══════════════════════════════════════════════════════════════════════════════
 *
 * Board:        Freenove ESP32-S3 WROOM CAM Board
 * Camera:       OV2640 or OV3660 (auto-detected)
 * PSRAM:        8MB OPI PSRAM (required for camera buffers)
 *
 * Communication:
 *   UART TX (GPIO43) -> Teensy RX1 (pin 0) @ 921600 baud
 *   UART RX (GPIO44) -> Teensy TX1 (pin 1) @ 921600 baud
 *   WiFi -> HTTP (port 80), WebSocket (port 81), UDP (port 8888)
 *
 * ═══════════════════════════════════════════════════════════════════════════════
 */

#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>   // Install: "WebSockets" by Markus Sattler (v2.4.0+)
#include <WiFiUdp.h>
#include "esp_camera.h"
#include "esp_task_wdt.h"
#include "esp_heap_caps.h"

// ════════════════════════════════════════════════════════════════
// CONFIGURATION — EDIT THESE
// ════════════════════════════════════════════════════════════════

// WiFi
const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// Camera resolution — higher than before since detection is on PC now
// Options: FRAMESIZE_QVGA (320x240), FRAMESIZE_VGA (640x480),
//          FRAMESIZE_SVGA (800x600), FRAMESIZE_XGA (1024x768)
// Recommendation: VGA for good balance of quality and frame rate
#define CAMERA_RESOLUTION FRAMESIZE_VGA

// JPEG quality: 10 = best quality (larger), 40 = lower quality (smaller/faster)
#define JPEG_QUALITY 12

// Target stream FPS (actual may be lower depending on resolution + WiFi)
#define TARGET_FPS 20

// ════════════════════════════════════════════════════════════════
// NETWORK PORTS
// ════════════════════════════════════════════════════════════════
#define HTTP_PORT       80    // MJPEG stream + snapshot
#define WEBSOCKET_PORT  81    // Command bridge (PC ↔ Teensy)
#define UDP_PORT        8888  // Face coordinate fast path (PC → Teensy)

// ════════════════════════════════════════════════════════════════
// TEENSY UART CONFIGURATION
// ════════════════════════════════════════════════════════════════
#define TEENSY_BAUD    921600
#define TEENSY_TX_PIN  43
#define TEENSY_RX_PIN  44
HardwareSerial TeensySerial(1);  // UART1

// ════════════════════════════════════════════════════════════════
// CAMERA PIN DEFINITIONS (Freenove ESP32-S3 WROOM)
// ════════════════════════════════════════════════════════════════
#define PWDN_GPIO     -1
#define RESET_GPIO    -1
#define XCLK_GPIO     15
#define SIOD_GPIO     4
#define SIOC_GPIO     5
#define Y9_GPIO       16
#define Y8_GPIO       17
#define Y7_GPIO       18
#define Y6_GPIO       12
#define Y5_GPIO       10
#define Y4_GPIO       8
#define Y3_GPIO       9
#define Y2_GPIO       11
#define VSYNC_GPIO    6
#define HREF_GPIO     7
#define PCLK_GPIO     13

// ════════════════════════════════════════════════════════════════
// WATCHDOG
// ════════════════════════════════════════════════════════════════
#define WDT_TIMEOUT_S 15

// ════════════════════════════════════════════════════════════════
// GLOBAL OBJECTS
// ════════════════════════════════════════════════════════════════
WebServer httpServer(HTTP_PORT);
WebSocketsServer wsServer(WEBSOCKET_PORT);
WiFiUDP udp;

bool wifiConnected = false;
bool teensyConnected = false;
volatile bool streamActive = false;

// Stream client tracking
WiFiClient streamClients[2];  // Max 2 simultaneous stream viewers
int numStreamClients = 0;

// Stats
unsigned long framesSent = 0;
unsigned long framesDropped = 0;
unsigned long udpReceived = 0;
unsigned long wsMessagesIn = 0;
unsigned long wsMessagesOut = 0;
unsigned long bootTime = 0;

// Teensy response buffer
char teensyRxBuffer[512];
int teensyRxPos = 0;

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
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;  // Direct JPEG — no RGB565 conversion needed
  config.grab_mode    = CAMERA_GRAB_LATEST;  // Always get latest frame, skip old ones

  // Higher resolution since we're not doing onboard detection
  if (psramFound()) {
    config.frame_size   = CAMERA_RESOLUTION;
    config.jpeg_quality = JPEG_QUALITY;
    config.fb_count     = 2;  // Double buffer for smoother streaming
    config.fb_in_psram  = true;
    Serial.println("[CAM] PSRAM found — using double buffer");
  } else {
    config.frame_size   = FRAMESIZE_QVGA;  // Fallback if no PSRAM
    config.jpeg_quality = 20;
    config.fb_count     = 1;
    Serial.println("[CAM] WARNING: No PSRAM — reduced resolution");
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("[CAM] Init failed: 0x%x\n", err);
    return false;
  }

  // Optimize sensor settings for streaming
  sensor_t* s = esp_camera_sensor_get();
  if (s) {
    s->set_framesize(s, (framesize_t)CAMERA_RESOLUTION);
    s->set_quality(s, JPEG_QUALITY);
    s->set_brightness(s, 1);    // Slightly brighter
    s->set_saturation(s, 0);    // Neutral
    s->set_whitebal(s, 1);      // Auto white balance ON
    s->set_awb_gain(s, 1);      // Auto WB gain ON
    s->set_exposure_ctrl(s, 1); // Auto exposure ON
    s->set_aec2(s, 1);          // AEC DSP ON
    s->set_gain_ctrl(s, 1);     // Auto gain ON
    s->set_hmirror(s, 0);       // Adjust if image is mirrored
    s->set_vflip(s, 0);         // Adjust if image is flipped
    Serial.println("[CAM] Sensor configured for streaming");
  }

  // Verify with test capture
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("[CAM] Test capture failed");
    return false;
  }
  Serial.printf("[CAM] Test frame: %dx%d, %d bytes\n", fb->width, fb->height, fb->len);
  esp_camera_fb_return(fb);

  return true;
}

// ════════════════════════════════════════════════════════════════
// WIFI SETUP
// ════════════════════════════════════════════════════════════════
bool setupWiFi() {
  Serial.printf("[WIFI] Connecting to %s", WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);  // Disable WiFi sleep for lower latency
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - start > 10000) {
      Serial.println("\n[WIFI] Connection timeout");
      return false;
    }
    delay(250);
    Serial.print(".");
    esp_task_wdt_reset();
  }

  Serial.printf("\n[WIFI] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
  Serial.printf("[WIFI] Signal: %d dBm\n", WiFi.RSSI());
  return true;
}

// ════════════════════════════════════════════════════════════════
// JOB 1: MJPEG STREAM
// ════════════════════════════════════════════════════════════════

// Boundary string for multipart MJPEG
#define MJPEG_BOUNDARY "BuddyCamFrame"

void handleStream() {
  WiFiClient client = httpServer.client();

  // Send multipart header
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: multipart/x-mixed-replace; boundary=" MJPEG_BOUNDARY);
  client.println("Access-Control-Allow-Origin: *");
  client.println("Cache-Control: no-cache");
  client.println("Connection: keep-alive");
  client.println();

  Serial.println("[STREAM] Client connected");
  streamActive = true;

  unsigned long frameInterval = 1000 / TARGET_FPS;
  unsigned long lastFrame = 0;

  while (client.connected()) {
    esp_task_wdt_reset();

    unsigned long now = millis();
    if (now - lastFrame < frameInterval) {
      delay(1);
      continue;
    }
    lastFrame = now;

    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
      framesDropped++;
      continue;
    }

    // Send MJPEG frame
    client.printf("--%s\r\n", MJPEG_BOUNDARY);
    client.println("Content-Type: image/jpeg");
    client.printf("Content-Length: %d\r\n", fb->len);
    client.printf("X-Timestamp: %lu\r\n", now);
    client.println();

    size_t written = client.write(fb->buf, fb->len);
    client.println();

    esp_camera_fb_return(fb);

    if (written == 0) {
      Serial.println("[STREAM] Write failed, client disconnected");
      break;
    }

    framesSent++;
  }

  streamActive = false;
  Serial.println("[STREAM] Client disconnected");
}

void handleCapture() {
  // Single JPEG snapshot — for LLM vision queries
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    httpServer.send(500, "text/plain", "Capture failed");
    return;
  }

  httpServer.sendHeader("Access-Control-Allow-Origin", "*");
  httpServer.sendHeader("Content-Disposition", "inline; filename=capture.jpg");
  httpServer.send_P(200, "image/jpeg", (const char*)fb->buf, fb->len);

  esp_camera_fb_return(fb);
}

void handleHealth() {
  char buf[256];
  snprintf(buf, sizeof(buf),
    "{"
    "\"status\":\"ok\","
    "\"uptime\":%lu,"
    "\"wifi_rssi\":%d,"
    "\"heap\":%u,"
    "\"psram\":%u,"
    "\"frames_sent\":%lu,"
    "\"frames_dropped\":%lu,"
    "\"udp_received\":%lu,"
    "\"ws_in\":%lu,"
    "\"ws_out\":%lu,"
    "\"stream_active\":%s,"
    "\"teensy\":%s"
    "}",
    (millis() - bootTime) / 1000,
    WiFi.RSSI(),
    (unsigned int)ESP.getFreeHeap(),
    (unsigned int)ESP.getFreePsram(),
    framesSent, framesDropped,
    udpReceived, wsMessagesIn, wsMessagesOut,
    streamActive ? "true" : "false",
    teensyConnected ? "true" : "false"
  );

  httpServer.sendHeader("Access-Control-Allow-Origin", "*");
  httpServer.send(200, "application/json", buf);
}

void handleRoot() {
  // Simple status page with embedded stream viewer
  String html = "<!DOCTYPE html><html><head><title>Buddy ESP32 Bridge</title>"
    "<style>body{background:#1a1a2e;color:#e0e0e0;font-family:monospace;padding:20px;}"
    "img{border:2px solid #333;border-radius:8px;}</style></head><body>"
    "<h2>Buddy ESP32-S3 Bridge</h2>"
    "<p>Stream: <a href='/stream'>/stream</a></p>"
    "<p>Snapshot: <a href='/capture'>/capture</a></p>"
    "<p>Health: <a href='/health'>/health</a></p>"
    "<p>WebSocket: ws://" + WiFi.localIP().toString() + ":" + String(WEBSOCKET_PORT) + "</p>"
    "<p>UDP Face Port: " + String(UDP_PORT) + "</p>"
    "<h3>Live Preview:</h3>"
    "<img src='/stream' width='640'>"
    "</body></html>";

  httpServer.send(200, "text/html", html);
}

// ════════════════════════════════════════════════════════════════
// JOB 2: WEBSOCKET BRIDGE (PC ↔ Teensy commands)
// ════════════════════════════════════════════════════════════════

// WebSocket timeout for Teensy response
#define TEENSY_RESPONSE_TIMEOUT_MS 200

void wsEvent(uint8_t clientNum, WStype_t type, uint8_t* payload, size_t length) {
  switch(type) {
    case WStype_CONNECTED: {
      IPAddress ip = wsServer.remoteIP(clientNum);
      Serial.printf("[WS] Client %u connected from %s\n", clientNum, ip.toString().c_str());
      // Send welcome with ESP32 info
      char welcome[128];
      snprintf(welcome, sizeof(welcome),
        "{\"type\":\"hello\",\"ip\":\"%s\",\"udp_port\":%d}",
        WiFi.localIP().toString().c_str(), UDP_PORT);
      wsServer.sendTXT(clientNum, welcome);
      break;
    }

    case WStype_DISCONNECTED:
      Serial.printf("[WS] Client %u disconnected\n", clientNum);
      break;

    case WStype_TEXT: {
      wsMessagesIn++;

      // Forward command to Teensy via UART
      // Commands arrive as plain text, e.g. "!QUERY\n" or "!NOD:2\n"
      char* cmd = (char*)payload;

      // Forward to Teensy
      TeensySerial.println(cmd);
      TeensySerial.flush();

      // Wait for Teensy response (with timeout)
      unsigned long waitStart = millis();
      String response = "";
      bool gotResponse = false;

      while (millis() - waitStart < TEENSY_RESPONSE_TIMEOUT_MS) {
        while (TeensySerial.available()) {
          char c = TeensySerial.read();
          response += c;
          if (c == '\n') {
            gotResponse = true;
            break;
          }
        }
        if (gotResponse) break;
        delayMicroseconds(100);
      }

      if (gotResponse) {
        response.trim();
        wsServer.sendTXT(clientNum, response.c_str());
        wsMessagesOut++;
      } else {
        wsServer.sendTXT(clientNum, "{\"ok\":false,\"reason\":\"timeout\"}");
        wsMessagesOut++;
      }
      break;
    }

    default:
      break;
  }
}

// ════════════════════════════════════════════════════════════════
// JOB 3: UDP FACE COORDINATE RELAY (PC → Teensy fast path)
// ════════════════════════════════════════════════════════════════

char udpBuffer[128];

void handleUDP() {
  int packetSize = udp.parsePacket();
  if (packetSize == 0) return;

  int len = udp.read(udpBuffer, sizeof(udpBuffer) - 1);
  if (len <= 0) return;
  udpBuffer[len] = '\0';
  udpReceived++;

  // Forward directly to Teensy — no parsing, no modification
  // PC sends "FACE:120,100,5,-3,45,50,85,42" or "NO_FACE,seq"
  // Teensy receives exactly that on Serial1
  TeensySerial.println(udpBuffer);

  // Note: NO flush here — we want minimum latency.
  // UART at 921600 baud sends 80 bytes in ~0.1ms, no need to wait.
}

// ════════════════════════════════════════════════════════════════
// TEENSY UNSOLICITED MESSAGE HANDLER
// ════════════════════════════════════════════════════════════════
// The Teensy might send unsolicited messages (like debug output).
// We forward these to any connected WebSocket client.

void checkTeensyUnsolicited() {
  while (TeensySerial.available()) {
    char c = TeensySerial.read();

    if (c == '\n' || teensyRxPos >= (int)(sizeof(teensyRxBuffer) - 1)) {
      teensyRxBuffer[teensyRxPos] = '\0';

      if (teensyRxPos > 0) {
        // Forward to WebSocket clients as unsolicited message
        // Wrap in JSON so PC can distinguish from command responses
        char wrapped[560];
        snprintf(wrapped, sizeof(wrapped), "{\"type\":\"unsolicited\",\"data\":\"%s\"}", teensyRxBuffer);
        wsServer.broadcastTXT(wrapped);
      }

      teensyRxPos = 0;
    } else {
      teensyRxBuffer[teensyRxPos++] = c;
    }
  }
}

// ════════════════════════════════════════════════════════════════
// WIFI RECONNECTION
// ════════════════════════════════════════════════════════════════
unsigned long lastWifiCheck = 0;
#define WIFI_CHECK_INTERVAL 30000

void checkWiFi() {
  if (millis() - lastWifiCheck < WIFI_CHECK_INTERVAL) return;
  lastWifiCheck = millis();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WIFI] Lost connection, reconnecting...");
    WiFi.disconnect();
    delay(100);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 5000) {
      esp_task_wdt_reset();
      delay(250);
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("[WIFI] Reconnected: %s\n", WiFi.localIP().toString().c_str());
    } else {
      Serial.println("[WIFI] Reconnect failed, will retry");
    }
  }
}

// ════════════════════════════════════════════════════════════════
// SETUP
// ════════════════════════════════════════════════════════════════
void setup() {
  // USB CDC serial for debug output
  Serial.begin(921600);
  delay(2000);

  bootTime = millis();

  Serial.println("\n╔════════════════════════════════════════════╗");
  Serial.println("║  BUDDY ESP32-S3 WiFi Bridge v1.0          ║");
  Serial.println("║  Camera Stream + WiFi↔UART Bridge         ║");
  Serial.println("╚════════════════════════════════════════════╝\n");

  // ── Initialize UART to Teensy ──
  TeensySerial.begin(TEENSY_BAUD, SERIAL_8N1, TEENSY_RX_PIN, TEENSY_TX_PIN);
  delay(100);
  TeensySerial.println("ESP32_READY");
  Serial.println("[UART] Teensy serial initialized (TX:43, RX:44, 921600 baud)");

  // Check for Teensy response
  unsigned long teensyWait = millis();
  while (millis() - teensyWait < 2000) {
    if (TeensySerial.available()) {
      String resp = TeensySerial.readStringUntil('\n');
      resp.trim();
      if (resp.length() > 0) {
        Serial.printf("[UART] Teensy says: %s\n", resp.c_str());
        teensyConnected = true;
        break;
      }
    }
    delay(50);
  }
  if (!teensyConnected) {
    Serial.println("[UART] WARNING: No Teensy response (may not be powered yet)");
  }

  // ── Initialize Camera ──
  Serial.println("[CAM] Initializing...");
  if (!initCamera()) {
    Serial.println("[CAM] FATAL: Camera init failed. Restarting in 5s...");
    delay(5000);
    ESP.restart();
  }
  Serial.println("[CAM] Ready");

  // ── Initialize WiFi ──
  wifiConnected = setupWiFi();
  if (!wifiConnected) {
    Serial.println("[WIFI] FATAL: Cannot proceed without WiFi. Restarting in 5s...");
    delay(5000);
    ESP.restart();
  }

  // ── Initialize HTTP Server (MJPEG stream + snapshots) ──
  httpServer.on("/", handleRoot);
  httpServer.on("/stream", HTTP_GET, handleStream);
  httpServer.on("/capture", HTTP_GET, handleCapture);
  httpServer.on("/health", HTTP_GET, handleHealth);
  httpServer.begin();
  Serial.printf("[HTTP] Server on port %d\n", HTTP_PORT);

  // ── Initialize WebSocket Server (command bridge) ──
  wsServer.begin();
  wsServer.onEvent(wsEvent);
  Serial.printf("[WS] Server on port %d\n", WEBSOCKET_PORT);

  // ── Initialize UDP (face coordinate fast path) ──
  udp.begin(UDP_PORT);
  Serial.printf("[UDP] Listening on port %d\n", UDP_PORT);

  // ── Initialize Watchdog ──
  esp_task_wdt_init(WDT_TIMEOUT_S, true);
  esp_task_wdt_add(NULL);

  // ── Print connection info ──
  Serial.println("\n╔════════════════════════════════════════════╗");
  Serial.printf("║  Stream:    http://%s/stream     \n", WiFi.localIP().toString().c_str());
  Serial.printf("║  Snapshot:  http://%s/capture    \n", WiFi.localIP().toString().c_str());
  Serial.printf("║  Health:    http://%s/health     \n", WiFi.localIP().toString().c_str());
  Serial.printf("║  WebSocket: ws://%s:%d           \n", WiFi.localIP().toString().c_str(), WEBSOCKET_PORT);
  Serial.printf("║  UDP Face:  %s:%d                \n", WiFi.localIP().toString().c_str(), UDP_PORT);
  Serial.println("╚════════════════════════════════════════════╝\n");

  Serial.println("[READY] ESP32 Bridge active. Waiting for connections...\n");
}

// ════════════════════════════════════════════════════════════════
// MAIN LOOP
// ════════════════════════════════════════════════════════════════
void loop() {
  esp_task_wdt_reset();

  // Handle HTTP requests (MJPEG stream runs in handleStream's while loop)
  httpServer.handleClient();

  // Handle WebSocket events
  wsServer.loop();

  // Handle incoming UDP face coordinates → forward to Teensy
  handleUDP();

  // Check for unsolicited Teensy messages (only when not in WebSocket request)
  // NOTE: This is only checked between WebSocket commands.
  // During a WS command, the response is handled in wsEvent directly.
  checkTeensyUnsolicited();

  // WiFi health check
  checkWiFi();

  // Periodic status log
  static unsigned long lastStatusLog = 0;
  if (millis() - lastStatusLog > 60000) {
    lastStatusLog = millis();
    Serial.printf("[STATUS] Up:%lus RSSI:%ddBm Heap:%u PSRAM:%u Frames:%lu UDP:%lu WS:%lu/%lu\n",
      (millis() - bootTime) / 1000,
      WiFi.RSSI(),
      (unsigned int)ESP.getFreeHeap(),
      (unsigned int)ESP.getFreePsram(),
      framesSent, udpReceived,
      wsMessagesIn, wsMessagesOut);
  }

  // Small yield to prevent watchdog issues
  delay(1);
}
