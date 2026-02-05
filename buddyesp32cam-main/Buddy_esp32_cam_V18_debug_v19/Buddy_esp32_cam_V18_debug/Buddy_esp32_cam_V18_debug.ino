/**
 * ═══════════════════════════════════════════════════════════════════════════════
 * BUDDY ESP32-CAM - Face Detection & Tracking System
 * ═══════════════════════════════════════════════════════════════════════════════
 *
 * Project:     Buddy Robot Vision System
 * Description: Real-time face detection with histogram-based tracking fallback.
 *              Outputs face position/velocity to Teensy via Serial at 50Hz.
 *              Includes HTTP server for JPEG frame capture on /capture endpoint.
 *
 * Version:     8.1.0
 * Date:        2025-02-04
 * Author:      Frank
 * Repository:  https://github.com/frosted123456/buddyesp32cam
 *
 * ═══════════════════════════════════════════════════════════════════════════════
 * ARDUINO IDE CONFIGURATION
 * ═══════════════════════════════════════════════════════════════════════════════
 *
 * Board Manager URL: https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
 * ESP32 Board Package: v2.0.14 or later (tested with 2.0.17)
 *
 * Tools Menu Settings:
 *   Board:              "ESP32S3 Dev Module"
 *   USB CDC On Boot:    "Enabled"
 *   CPU Frequency:      "240MHz (WiFi)"
 *   Core Debug Level:   "None"
 *   USB DFU On Boot:    "Disabled"
 *   Erase All Flash:    "Disabled"
 *   Events Run On:      "Core 1"
 *   Flash Mode:         "QIO 80MHz"
 *   Flash Size:         "16MB (128Mb)"
 *   JTAG Adapter:       "Disabled"
 *   Arduino Runs On:    "Core 1"
 *   USB Firmware MSC:   "Disabled"
 *   Partition Scheme:   "Huge APP (3MB No OTA/1MB SPIFFS)"
 *   PSRAM:              "OPI PSRAM"
 *   Upload Mode:        "UART0 / Hardware CDC"
 *   Upload Speed:       "921600"
 *   USB Mode:           "Hardware CDC and JTAG"
 *
 * ═══════════════════════════════════════════════════════════════════════════════
 * REQUIRED LIBRARIES
 * ═══════════════════════════════════════════════════════════════════════════════
 *
 * Library Name                  | Version | Source
 * ------------------------------|---------|----------------------------------------
 * EloquentEsp32cam              | 2.6.0+  | Arduino Library Manager / GitHub
 * ESP32 Arduino Core            | 2.0.14+ | Board Manager (includes WiFi, WebServer)
 *
 * Built-in (no install needed):
 *   - WiFi.h          (ESP32 core)
 *   - WebServer.h     (ESP32 core)
 *   - esp_camera.h    (ESP32 core)
 *
 * ═══════════════════════════════════════════════════════════════════════════════
 * HARDWARE CONFIGURATION
 * ═══════════════════════════════════════════════════════════════════════════════
 *
 * Board:        Freenove ESP32-S3 WROOM CAM Board
 * Camera:       OV2640 or OV3660 (auto-detected)
 * PSRAM:        8MB OPI PSRAM (required for camera buffers)
 *
 * Pin Assignments (Freenove S3 - handled by library):
 *   PWDN_GPIO    = -1 (not used)
 *   RESET_GPIO   = -1 (not used)
 *   XCLK_GPIO    = 15
 *   SIOD_GPIO    = 4
 *   SIOC_GPIO    = 5
 *   Y9_GPIO      = 16
 *   Y8_GPIO      = 17
 *   Y7_GPIO      = 18
 *   Y6_GPIO      = 12
 *   Y5_GPIO      = 10
 *   Y4_GPIO      = 8
 *   Y3_GPIO      = 9
 *   Y2_GPIO      = 11
 *   VSYNC_GPIO   = 6
 *   HREF_GPIO   = 7
 *   PCLK_GPIO    = 13
 *
 * Communication:
 *   Serial TX    -> Teensy RX (921600 baud)
 *   WiFi         -> HTTP server on port 80
 *
 * ═══════════════════════════════════════════════════════════════════════════════
 * WIFI CONFIGURATION
 * ═══════════════════════════════════════════════════════════════════════════════
 *
 * Edit WIFI_SSID and WIFI_PASSWORD constants below before uploading.
 * The device IP address will be printed to Serial on successful connection.
 * Access the camera feed at: http://<IP_ADDRESS>/capture
 *
 * ═══════════════════════════════════════════════════════════════════════════════
 * VERSION HISTORY
 * ═══════════════════════════════════════════════════════════════════════════════
 *
 * v8.1.0 (2025-02-04)
 *   - Fixed HTTP capture timeout/crash: added watchdog timer reset
 *   - Fixed frame buffer leak in HTTP capture handler
 *   - Added /health endpoint for lightweight connectivity testing
 *   - Added automatic camera reinitialization on repeated failures
 *   - Added WiFi auto-reconnection
 *   - Added memory pressure detection with adaptive JPEG quality
 *   - Added free heap/PSRAM monitoring in health logs
 *
 * v8.0.0 (2025-02-01)
 *   - Added WiFi connectivity
 *   - Added HTTP server with /capture JPEG endpoint
 *   - Added explicit frame buffer management
 *   - Fixed integer overflow in velocity calculation
 *   - Improved error recovery in camera initialization
 *   - Consolidated magic numbers into named constants
 *   - Optimized frame rotation (removed redundant memcpy)
 *
 * v7.2.1 (previous)
 *   - Fixed velocity calculation overflow bug
 *   - Velocity synced to output timing (50Hz)
 *
 * v7.1.0 (previous)
 *   - Removed alpha-beta filtering and prediction
 *   - Simplified to direct measurements
 *
 * ═══════════════════════════════════════════════════════════════════════════════
 * LICENSE
 * ═══════════════════════════════════════════════════════════════════════════════
 *
 * This project is provided as-is for the Buddy robot project.
 *
 * ═══════════════════════════════════════════════════════════════════════════════
 */

#include <eloquent_esp32cam.h>
#include <eloquent_esp32cam/face/detection.h>
#include "esp_camera.h"
#include "img_converters.h"  // For frame2jpg() - RGB565 to JPEG conversion
#include "HistogramTracker.h"
#include <WiFi.h>
#include <WebServer.h>
#include "esp_task_wdt.h"    // Watchdog timer control
#include "esp_heap_caps.h"   // Heap/PSRAM monitoring

using eloq::camera;
using eloq::face::detection;

// ============================================
// WIFI CONFIGURATION - EDIT THESE VALUES
// ============================================
const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
const int WIFI_CONNECT_TIMEOUT_MS = 10000;
const int WIFI_RETRY_DELAY_MS = 500;

// ============================================
// CAMERA CONFIGURATION
// ============================================
const int CAMERA_WIDTH = 240;
const int CAMERA_HEIGHT = 240;
const int CENTER_X = 120;
const int CENTER_Y = 120;

// ============================================
// DETECTION CONFIGURATION
// ============================================
const int MIN_FACE_SIZE = 25;
const float DETECTION_CONFIDENCE = 0.5;
const int MIN_VALID_COORD = 20;
const int MAX_VALID_COORD = 220;
const int MIN_OUTPUT_CONFIDENCE = 25;
const unsigned long STALE_TIMEOUT_MS = 400;

// ============================================
// TIMING CONFIGURATION
// ============================================
const int SERIAL_BAUD = 921600;
const unsigned long OUTPUT_INTERVAL_MS = 20;      // 50Hz output
const unsigned long HEALTH_LOG_INTERVAL_MS = 60000;
const unsigned long FAILURE_LOG_INTERVAL_MS = 30000;
const float MAX_FAILURE_RATE_PERCENT = 10.0;

// ============================================
// HTTP SERVER CONFIGURATION
// ============================================
const int HTTP_SERVER_PORT = 80;
const int JPEG_QUALITY = 12;           // 0-63, lower = better quality
const int JPEG_QUALITY_LOW_MEM = 25;   // Reduced quality when memory is tight

// ============================================
// WATCHDOG & RECOVERY CONFIGURATION
// ============================================
const int WDT_TIMEOUT_S = 15;                     // Watchdog timeout in seconds
const int CAMERA_REINIT_FAILURE_THRESHOLD = 50;    // Consecutive failures before reinit
const unsigned long WIFI_RECONNECT_INTERVAL_MS = 30000;  // Check WiFi every 30s
const size_t LOW_MEMORY_THRESHOLD = 40000;         // Free heap below this = memory pressure

// ============================================
// GLOBAL OBJECTS
// ============================================
WebServer server(HTTP_SERVER_PORT);
bool wifiConnected = false;
bool enableRotation = true;
uint8_t* rotationBuffer = NULL;

// ============================================
// SIMPLE STATE - Direct measurements
// ============================================
struct SimpleState {
  bool facePresent;
  int x, y;           // Current position (no filtering!)
  int lastX, lastY;   // Previous position (for velocity)
  int w, h;           // Face size
  int confidence;     // 0-100
  unsigned long lastUpdateTime;
  unsigned long lastOutputTime;
  
  void update(int newX, int newY, int width, int height, int conf) {
    // Update current position (lastX/lastY managed by getVelocity)
    x = newX;
    y = newY;
    w = width;
    h = height;
    confidence = conf;
    facePresent = true;
    lastUpdateTime = millis();
  }
  
  void getVelocity(int& vx, int& vy) {
    // Simple velocity: change in position since last output
    unsigned long now = millis();
    unsigned long dt = now - lastOutputTime;

    if (lastOutputTime > 0 && dt > 0 && dt < 100) {  // Reasonable time window
      // Calculate velocity in pixels/second (cast to long to prevent overflow)
      vx = (int)(((long)(x - lastX) * 1000L) / (long)dt);
      vy = (int)(((long)(y - lastY) * 1000L) / (long)dt);
    } else {
      vx = 0;
      vy = 0;
    }

    // Update for next calculation
    lastX = x;
    lastY = y;
    lastOutputTime = now;
  }
  
  void markLost() {
    facePresent = false;
    confidence = 0;
  }
  
  bool isStale(unsigned long maxAge = STALE_TIMEOUT_MS) const {
    return (millis() - lastUpdateTime) > maxAge;
  }
};

SimpleState state;
HistogramTracker tracker;

// ============================================
// CONFIDENCE CALCULATION
// ============================================
int calculateConfidence(bool aiDetection, bool histogramValid, float histConf, 
                       int faceW, int faceH) {
  if (aiDetection) {
    // AI detection confidence based on face size
    int sizeScore = constrain(faceW + faceH, 50, 150);
    return 70 + (sizeScore - 50) / 4;  // 70-95 range
  } else if (histogramValid) {
    // Histogram confidence (already 0-1 from tracker)
    return (int)(histConf * 100.0);
  }
  return 0;
}

// ============================================
// FRAME ROTATION
// ============================================
void rotateFrame90CCW(uint8_t* input, uint8_t* output, int width, int height) {
  for (int new_y = 0; new_y < height; new_y++) {
    for (int new_x = 0; new_x < width; new_x++) {
      int old_x = (width - 1) - new_y;
      int old_y = new_x;
      int old_idx = (old_y * width + old_x) * 2;
      int new_idx = (new_y * width + new_x) * 2;
      output[new_idx] = input[old_idx];
      output[new_idx + 1] = input[old_idx + 1];
    }
  }
}

bool captureAndRotate() {
  if (!camera.capture().isOk()) return false;
  if (enableRotation && rotationBuffer != NULL) {
    rotateFrame90CCW(camera.frame->buf, rotationBuffer, 240, 240);
    memcpy(camera.frame->buf, rotationBuffer, 240 * 240 * 2);
  }
  return true;
}

// ============================================
// CAPTURE WITH RETRY
// ============================================
bool captureWithRetry(int maxAttempts = 3) {
  for (int attempt = 0; attempt < maxAttempts; attempt++) {
    if (captureAndRotate()) {
      return true;  // Success
    }

    // Exponential backoff: 2ms, 4ms, 8ms
    delay(2 << attempt);
  }
  return false;  // All attempts failed
}

// ============================================
// CAMERA REINITIALIZATION
// ============================================
// Call this when capture failures exceed threshold.
// Deinitializes and reinitializes the camera peripheral.
bool reinitializeCamera() {
  Serial.println("[REINIT] Attempting camera reinitialization...");

  // Deinitialize the camera peripheral
  esp_camera_deinit();
  delay(500);

  // Reconfigure and reinitialize
  camera.pinout.freenove_s3();
  camera.brownout.disable();
  camera.resolution.face();
  camera.pixformat.rgb565();
  camera.quality.high();

  if (!camera.begin().isOk()) {
    Serial.println("[REINIT] Camera reinitialization FAILED");
    return false;
  }

  // Verify with a test capture
  if (!captureAndRotate()) {
    Serial.println("[REINIT] Post-reinit test capture FAILED");
    return false;
  }

  Serial.println("[REINIT] Camera reinitialized successfully");
  return true;
}

// ============================================
// WIFI SETUP
// ============================================
bool setupWiFi() {
  Serial.print("Connecting to WiFi: ");
  Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long startTime = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - startTime > WIFI_CONNECT_TIMEOUT_MS) {
      Serial.println("\nWiFi connection TIMEOUT");
      return false;
    }
    delay(WIFI_RETRY_DELAY_MS);
    Serial.print(".");
  }

  Serial.println("\nWiFi connected!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
  return true;
}

// ============================================
// HTTP SERVER HANDLERS
// ============================================

// Flag to prevent concurrent capture conflicts
volatile bool jpegCaptureInProgress = false;

void handleHealth() {
  // Lightweight endpoint - no capture, no JPEG conversion
  // Use this for connectivity testing instead of /capture
  size_t freeHeap = ESP.getFreeHeap();
  size_t freePsram = ESP.getFreePsram();

  char response[128];
  snprintf(response, sizeof(response),
           "OK\nheap:%u\npsram:%u\nwifi:%d\nuptime:%lu",
           (unsigned int)freeHeap, (unsigned int)freePsram,
           WiFi.status() == WL_CONNECTED ? 1 : 0,
           millis() / 1000);
  server.send(200, "text/plain", response);
}

void handleCapture() {
  // Prevent concurrent captures that could corrupt state
  if (jpegCaptureInProgress) {
    server.send(503, "text/plain", "Capture already in progress");
    return;
  }
  jpegCaptureInProgress = true;

  // Reset watchdog - capture + JPEG conversion can take a moment
  esp_task_wdt_reset();

  Serial.println("[HTTP] /capture request received");

  // Capture a fresh RGB565 frame using the existing camera setup
  if (!camera.capture().isOk()) {
    jpegCaptureInProgress = false;
    server.send(500, "text/plain", "Failed to capture frame");
    Serial.println("[HTTP] Capture failed");
    return;
  }

  // Adapt JPEG quality based on available memory
  size_t freeHeap = ESP.getFreeHeap();
  int quality = (freeHeap < LOW_MEMORY_THRESHOLD) ? JPEG_QUALITY_LOW_MEM : JPEG_QUALITY;
  if (freeHeap < LOW_MEMORY_THRESHOLD) {
    Serial.print("[HTTP] Low memory (");
    Serial.print(freeHeap);
    Serial.println("), using reduced JPEG quality");
  }

  // Convert RGB565 to JPEG using ESP32's built-in converter
  uint8_t* jpegBuf = NULL;
  size_t jpegLen = 0;

  // frame2jpg is provided by esp_camera.h - converts RGB565 to JPEG
  bool converted = frame2jpg(camera.frame, quality, &jpegBuf, &jpegLen);

  if (!converted || jpegBuf == NULL || jpegLen == 0) {
    // Ensure partial allocation is freed
    if (jpegBuf != NULL) {
      free(jpegBuf);
    }
    jpegCaptureInProgress = false;
    server.send(500, "text/plain", "JPEG conversion failed");
    Serial.println("[HTTP] JPEG conversion failed");
    return;
  }

  Serial.print("[HTTP] JPEG size: ");
  Serial.print(jpegLen);
  Serial.println(" bytes");

  // Reset watchdog before network send (can be slow)
  esp_task_wdt_reset();

  // Send JPEG response
  server.sendHeader("Content-Disposition", "inline; filename=capture.jpg");
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Cache-Control", "no-cache");
  server.send_P(200, "image/jpeg", (const char*)jpegBuf, jpegLen);

  // Free the JPEG buffer (allocated by frame2jpg)
  free(jpegBuf);
  jpegBuf = NULL;

  jpegCaptureInProgress = false;
  Serial.println("[HTTP] Capture complete");
}

void handleNotFound() {
  String message = "Buddy ESP32-CAM\n\n";
  message += "Available endpoints:\n";
  message += "  GET /health  - Health check (no capture)\n";
  message += "  GET /capture - Returns current JPEG frame\n";
  server.send(404, "text/plain", message);
}

void setupHttpServer() {
  server.on("/health", HTTP_GET, handleHealth);
  server.on("/capture", HTTP_GET, handleCapture);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("HTTP server started on port 80");
  Serial.print("Health URL: http://");
  Serial.print(WiFi.localIP());
  Serial.println("/health");
  Serial.print("Capture URL: http://");
  Serial.print(WiFi.localIP());
  Serial.println("/capture");
}

// ============================================
// SETUP
// ============================================
void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(2000);

  Serial.println("\n╔════════════════════════════════════════╗");
  Serial.println("║  BUDDY ESP32-CAM v8.1.0               ║");
  Serial.println("║  Face Detection + HTTP Server         ║");
  Serial.println("╚════════════════════════════════════════╝\n");

  // Initialize state
  memset(&state, 0, sizeof(SimpleState));
  state.x = CENTER_X;
  state.y = CENTER_Y;
  state.lastX = CENTER_X;
  state.lastY = CENTER_Y;
  state.facePresent = false;

  // Camera configuration
  camera.pinout.freenove_s3();
  camera.brownout.disable();
  camera.resolution.face();
  camera.pixformat.rgb565();
  camera.quality.high();
  detection.accurate();
  detection.confidence(DETECTION_CONFIDENCE);

  Serial.println("Configuration:");
  Serial.println("  - Single core execution");
  Serial.println("  - Direct measurements (no filtering)");
  Serial.println("  - Output: 50Hz to Teensy via Serial");
  Serial.println("  - HTTP server for JPEG capture");
  Serial.println("  - Immediate histogram recovery");
  Serial.println();

  // Allocate rotation buffer
  if (enableRotation) {
    rotationBuffer = (uint8_t*)ps_malloc(CAMERA_WIDTH * CAMERA_HEIGHT * 2);
    if (!rotationBuffer) {
      Serial.println("ERROR: Rotation buffer allocation failed");
      Serial.println("Continuing without rotation...");
      enableRotation = false;
    }
  }

  // Initialize camera with retry
  Serial.print("Initializing camera");
  int cameraRetries = 3;
  bool cameraOk = false;
  while (cameraRetries > 0 && !cameraOk) {
    if (camera.begin().isOk()) {
      cameraOk = true;
    } else {
      cameraRetries--;
      Serial.print(".");
      delay(1000);
    }
  }
  if (!cameraOk) {
    Serial.println(" FAILED after 3 attempts!");
    Serial.println("Please check camera connection and restart.");
    while (1) {
      delay(5000);
      Serial.println("Camera init failed - restart required");
    }
  }
  Serial.println(" OK");

  // Test capture
  Serial.print("Testing capture... ");
  if (!captureAndRotate()) {
    Serial.println("FAILED!");
    Serial.println("Camera initialized but capture failed.");
    while (1) {
      delay(5000);
      Serial.println("Capture test failed - restart required");
    }
  }
  Serial.println("OK");

  // Initialize WiFi
  Serial.println();
  wifiConnected = setupWiFi();
  if (wifiConnected) {
    setupHttpServer();
  } else {
    Serial.println("WARNING: WiFi not connected - HTTP server disabled");
    Serial.println("Face detection will continue without network features.");
  }

  // Initialize watchdog timer
  esp_task_wdt_init(WDT_TIMEOUT_S, true);  // true = panic (reboot) on timeout
  esp_task_wdt_add(NULL);  // Add current task (loop task) to watchdog
  Serial.print("Watchdog timer initialized (");
  Serial.print(WDT_TIMEOUT_S);
  Serial.println("s timeout)");

  Serial.println("\n╔════════════════════════════════════════╗");
  Serial.println("║  SYSTEM ACTIVE                        ║");
  if (wifiConnected) {
    Serial.print("║  HTTP: http://");
    Serial.print(WiFi.localIP());
    String padding = "";
    int ipLen = WiFi.localIP().toString().length();
    for (int i = 0; i < (17 - ipLen); i++) padding += " ";
    Serial.print(padding);
    Serial.println("║");
  }
  Serial.println("╚════════════════════════════════════════╝\n");

  delay(500);
}

// ============================================
// MAIN LOOP
// ============================================
void loop() {
  static unsigned long lastOutputTime = 0;
  static unsigned long messageCounter = 0;
  static unsigned long captureFailures = 0;
  static unsigned long consecutiveFailures = 0;
  static unsigned long totalCaptures = 0;
  static unsigned long lastWifiCheck = 0;

  // Reset watchdog at top of every loop iteration
  esp_task_wdt_reset();

  // Handle HTTP requests (non-blocking, ~0-1ms if no request)
  if (wifiConnected) {
    server.handleClient();
  }

  unsigned long now = millis();

  // ═══════════════════════════════════════════════════════
  // WIFI RECONNECTION CHECK (every 30 seconds)
  // ═══════════════════════════════════════════════════════
  if (wifiConnected && (now - lastWifiCheck > WIFI_RECONNECT_INTERVAL_MS)) {
    lastWifiCheck = now;
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("[WIFI] Connection lost, attempting reconnect...");
      WiFi.disconnect();
      delay(100);
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
      unsigned long reconnectStart = millis();
      while (WiFi.status() != WL_CONNECTED && (millis() - reconnectStart < 5000)) {
        esp_task_wdt_reset();
        delay(250);
      }
      if (WiFi.status() == WL_CONNECTED) {
        Serial.print("[WIFI] Reconnected, IP: ");
        Serial.println(WiFi.localIP());
      } else {
        Serial.println("[WIFI] Reconnect failed, will retry later");
      }
    }
  }

  // ═══════════════════════════════════════════════════════
  // CAPTURE WITH RETRY - More robust frame acquisition
  // ═══════════════════════════════════════════════════════
  totalCaptures++;

  if (!captureWithRetry(3)) {
    // All retries failed
    captureFailures++;
    consecutiveFailures++;

    // Log failure rate periodically (every 100 failures or 30 seconds)
    static unsigned long lastFailureLog = 0;
    if (captureFailures % 100 == 0 || (now - lastFailureLog > FAILURE_LOG_INTERVAL_MS && captureFailures > 0)) {
      float failRate = (captureFailures * 100.0) / totalCaptures;
      Serial.print("[WARN] Capture failures: ");
      Serial.print(captureFailures);
      Serial.print("/");
      Serial.print(totalCaptures);
      Serial.print(" (");
      Serial.print(failRate, 1);
      Serial.println("%)");
      lastFailureLog = now;

      // If failure rate too high, something is wrong
      if (failRate > MAX_FAILURE_RATE_PERCENT) {
        Serial.println("[ERROR] High capture failure rate - check camera connection");
      }
    }

    // Auto-reinitialize camera after too many consecutive failures
    if (consecutiveFailures >= CAMERA_REINIT_FAILURE_THRESHOLD) {
      Serial.print("[RECOVERY] ");
      Serial.print(consecutiveFailures);
      Serial.println(" consecutive failures - reinitializing camera");
      esp_task_wdt_reset();
      if (reinitializeCamera()) {
        consecutiveFailures = 0;
        Serial.println("[RECOVERY] Camera recovered");
      } else {
        Serial.println("[RECOVERY] Reinit failed, will keep retrying");
        consecutiveFailures = 0;  // Reset counter to avoid rapid reinit loops
        delay(1000);
        esp_task_wdt_reset();
      }
    }

    delay(10);  // Longer delay after complete failure
    return;
  }

  // Reset consecutive failure counter on successful capture
  consecutiveFailures = 0;
  
  bool faceFound = false;
  int detectedX = 0, detectedY = 0;
  int detectedW = 0, detectedH = 0;
  int confidence = 0;
  
  // ───────────────────────────────────────────────────────
  // TRY AI DETECTION FIRST
  // ───────────────────────────────────────────────────────
  // Reset watchdog before AI detection (can take 50-100ms)
  esp_task_wdt_reset();
  if (detection.run().isOk() && detection.count() > 0) {
    int faceX = detection.first.x;
    int faceY = detection.first.y;
    int faceW = detection.first.width;
    int faceH = detection.first.height;
    int centerX = faceX + (faceW / 2);
    int centerY = faceY + (faceH / 2);
    
    // Validate detection
    if (faceW >= MIN_FACE_SIZE && faceH >= MIN_FACE_SIZE &&
        centerX >= MIN_VALID_COORD && centerX <= MAX_VALID_COORD &&
        centerY >= MIN_VALID_COORD && centerY <= MAX_VALID_COORD) {
      
      faceFound = true;
      detectedX = centerX;
      detectedY = centerY;
      detectedW = faceW;
      detectedH = faceH;
      confidence = calculateConfidence(true, false, 0.0, faceW, faceH);
      
      // Build histogram signature for future tracking
      tracker.buildSignature(camera.frame->buf, centerX, centerY, faceW, faceH);
    }
  }
  
  // ───────────────────────────────────────────────────────
  // IF AI FAILED, TRY HISTOGRAM IMMEDIATELY (same frame!)
  // ───────────────────────────────────────────────────────
  if (!faceFound && tracker.isSignatureValid()) {
    float histConf = 0.0;
    int trackX = 0, trackY = 0;
    
    // Use last known position as search hint
    int predictX = state.x;
    int predictY = state.y;
    
    if (tracker.track(camera.frame->buf, trackX, trackY, histConf, 
                     predictX, predictY, 0.0)) {
      faceFound = true;
      detectedX = trackX;
      detectedY = trackY;
      detectedW = state.w;  // Use last known size
      detectedH = state.h;
      confidence = calculateConfidence(false, true, histConf, detectedW, detectedH);
    }
  }
  
  // ───────────────────────────────────────────────────────
  // UPDATE STATE with direct measurement
  // ───────────────────────────────────────────────────────
  if (faceFound) {
    state.update(detectedX, detectedY, detectedW, detectedH, confidence);
  } else {
    // No face found - check if stale
    if (state.isStale(STALE_TIMEOUT_MS)) {
      state.markLost();
    }
  }
  
  // ═══════════════════════════════════════════════════════
  // OUTPUT @ 50Hz (every 20ms)
  // ═══════════════════════════════════════════════════════
  if (now - lastOutputTime >= OUTPUT_INTERVAL_MS) {
    lastOutputTime = now;
    messageCounter++;
    
    // Calculate simple velocity
    int vx = 0, vy = 0;
    state.getVelocity(vx, vy);
    
    // Format output
    char buffer[80];
    if (state.facePresent && state.confidence >= MIN_OUTPUT_CONFIDENCE) {
      snprintf(buffer, sizeof(buffer), "FACE:%d,%d,%d,%d,%d,%d,%d,%lu",
               state.x, state.y, vx, vy,
               state.w, state.h, 
               state.confidence, messageCounter);
    } else {
      snprintf(buffer, sizeof(buffer), "NO_FACE,%lu", messageCounter);
    }
    
    Serial.println(buffer);

    // Periodic health stats with memory monitoring
    static unsigned long lastHealthLog = 0;
    if (now - lastHealthLog > HEALTH_LOG_INTERVAL_MS) {
      float failRate = (totalCaptures > 0) ? (captureFailures * 100.0) / totalCaptures : 0;
      Serial.print("[HEALTH] Captures: ");
      Serial.print(totalCaptures);
      Serial.print(", Failures: ");
      Serial.print(captureFailures);
      Serial.print(" (");
      Serial.print(failRate, 1);
      Serial.println("%)");

      // Memory stats
      Serial.print("[HEALTH] Free heap: ");
      Serial.print(ESP.getFreeHeap());
      Serial.print(", Free PSRAM: ");
      Serial.print(ESP.getFreePsram());
      Serial.print(", Min free heap: ");
      Serial.println(ESP.getMinFreeHeap());

      if (ESP.getFreeHeap() < LOW_MEMORY_THRESHOLD) {
        Serial.println("[WARN] Low free heap - possible memory fragmentation");
      }

      lastHealthLog = now;
    }
  }

  // Reset watchdog before yielding
  esp_task_wdt_reset();

  // Small delay to prevent tight spinning
  delay(1);
}
