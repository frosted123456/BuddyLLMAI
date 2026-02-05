/*
 * Buddy Robot V15 - Phase 3: Vision Integration
 * ESP32-S3 face detection integrated with Teensy behavior system
 * 
 * Hardware: Teensy 4.0 + ESP32-S3 CAM
 * Servos: 3x (base, nod, tilt)
 * Sensors: HC-SR04 Ultrasonic + ESP32-S3 Camera
 * 
 * NEW IN V15:
 * - ESP32-S3 serial communication (Serial1)
 * - Face detection triggers social behaviors
 * - Spatial memory tracks people
 * - Social awareness system active
 */

#include <Servo.h>

// ============================================
// PIN DEFINITIONS
// ============================================
#include "LittleBots_Board_Pins.h"

#define BASE_SERVO_PIN 2
#define NOD_SERVO_PIN 3
#define TILT_SERVO_PIN 4

// ============================================
// INCLUDE BEHAVIOR SYSTEM
// ============================================
#include "Needs.h"
#include "Personality.h"
#include "Emotion.h"
#include "BehaviorSelection.h"
#include "MovementStyle.h"
#include "SpatialMemory.h"
#include "Learning.h"
#include "AttentionSystem.h"
#include "ScanningSystem.h"
#include "IllusionLayer.h"
#include "ServoController.h"
#include "PoseLibrary.h"
#include "AnimationController.h"
#include "BodySchema.h"
#include "BehaviorEngine.h"
#include "ReflexiveControl.h"  // NEW: Reflexive tracking layer
#include "AIBridge.h"          // AI serial command integration

// ============================================
// VISION DATA STRUCTURES (PACKAGE 3)
// ============================================
struct FaceData {
  bool detected;
  int x;           // 0-240 (camera frame)
  int y;           // 0-240
  int size;        // Face size (pixels)
  int confidence;  // 0-100
  int personID;    // Person identifier
  int distance;    // Estimated distance (cm)
  unsigned long timestamp;  // NEW: ESP32 timestamp
  unsigned long sequence;   // NEW: Message sequence number
  unsigned long lastSeen;   // Teensy receive time

  FaceData() {
    detected = false;
    x = 120;
    y = 120;
    size = 0;
    confidence = 0;
    personID = -1;
    distance = 100;
    timestamp = 0;
    sequence = 0;
    lastSeen = 0;
  }
};

FaceData currentFace;

// ESP32-S3 Communication
#define ESP32_SERIAL Serial1  // Teensy pins 0 (RX1) and 1 (TX1)
#define ESP32_BAUD 921600     // MUST match ESP32 v7.2.1 baud rate

// Serial communication health tracking
unsigned long esp32LastMessage = 0;
unsigned long esp32MessageCount = 0;
unsigned long esp32ParseErrors = 0;
const unsigned long ESP32_TIMEOUT = 5000;  // 5 seconds without messages = warning

// ============================================
// GLOBAL OBJECTS
// ============================================
Servo baseServo;
Servo nodServo;
Servo tiltServo;

ServoController servoController;
AnimationController animator(servoController);
BehaviorEngine behaviorEngine;
ReflexiveControl reflexController;  // NEW: Reflexive tracking layer
AIBridge aiBridge;                  // AI serial command bridge

// Face tracking mode toggle
bool faceTrackingMode = false;

// ============================================
// HELPER STRUCTURES
// ============================================
struct headPos {
  int baseServoAngle;
  int nodServoAngle;
  int tiltServoAngle;
  int desiredDelay;
};

// ============================================
// TIMING VARIABLES
// ============================================
// ═══════════════════════════════════════════════════════════════
// PERFORMANCE: Increased update rate for smoother tracking
// ═══════════════════════════════════════════════════════════════
// Old: 100ms = 10Hz (sluggish, noticeable lag)
// New: 20ms = 50Hz (smooth, responsive tracking - 5x improvement!)
// CPU can handle this easily: 9µs loop time << 20ms interval
// CPU usage: 9µs/20ms = 0.045% = plenty of headroom
// ═══════════════════════════════════════════════════════════════
unsigned long lastUpdate = 0;
unsigned long lastDiagnostics = 0;
const unsigned long UPDATE_INTERVAL = 20;       // 20ms = 50Hz (5x smoother!)
const unsigned long DIAGNOSTICS_INTERVAL = 300000; // 5 minutes

// ═══════════════════════════════════════════════════════════════
// FRESH DATA TRACKING: Critical for preventing stale data movement
// ═══════════════════════════════════════════════════════════════
// ESP32 sends updates at ~8-10Hz (100-120ms)
// Reflex runs at 50Hz (20ms)
// Must ONLY move on fresh data to prevent overshoot!
bool freshFaceDataReceived = false;
unsigned long lastFaceDataTime = 0;

// ============================================
// FACE DETECTION HANDLER (PACKAGE 5: With Face Tracking)
// ============================================
void handleFaceDetection() {
  // Only process if confidence is reasonable (lowered to accept histogram tracking)
  if (currentFace.confidence < 30) {
    return;  // Too uncertain
  }

  // ==========================================
  // Calculate direction (existing logic)
  // ==========================================

  int centerX = 120;
  int deltaX = currentFace.x - centerX;

  int direction = 0;  // Forward by default

  if (abs(deltaX) > 20) {
    if (deltaX < -60) {
      direction = 6;  // Far left
    } else if (deltaX < -20) {
      direction = 7;  // Left
    } else if (deltaX > 60) {
      direction = 2;  // Far right
    } else if (deltaX > 20) {
      direction = 1;  // Right
    }
  }

  // ==========================================
  // Estimate distance from face size
  // ==========================================

  float estimatedDistance = 100.0;
  if (currentFace.size > 80) {
    estimatedDistance = 30.0;  // Very close
  } else if (currentFace.size > 60) {
    estimatedDistance = 50.0;  // Close
  } else if (currentFace.size > 40) {
    estimatedDistance = 80.0;  // Medium
  } else {
    estimatedDistance = 120.0;  // Far
  }

  // ==========================================
  // Person-specific response WITH face tracking
  // ==========================================

  if (currentFace.personID >= 0) {
    // Known person - handle relationship and tracking
    behaviorEngine.handlePersonDetection(currentFace.personID, estimatedDistance);

    // Start or update face tracking (intensity based on familiarity)
    if (!behaviorEngine.getIsTrackingFace()) {
      behaviorEngine.startFaceTracking(currentFace.personID, currentFace.x, currentFace.y);
    } else {
      behaviorEngine.updateFaceTracking(currentFace.x, currentFace.y);
    }
  } else {
    // Unknown person - generic social response
    Needs& needs = behaviorEngine.getNeeds();
    needs.satisfySocial(0.15);

    // Track anyway (generic intensity)
    if (!behaviorEngine.getIsTrackingFace()) {
      behaviorEngine.startFaceTracking(-1, currentFace.x, currentFace.y);
    } else {
      behaviorEngine.updateFaceTracking(currentFace.x, currentFace.y);
    }
  }

  // ==========================================
  // Update spatial memory and attention
  // ==========================================

  SpatialMemory& spatialMemory = behaviorEngine.getSpatialMemory();
  spatialMemory.recordFaceAt(direction, estimatedDistance);

  AttentionSystem& attention = behaviorEngine.getAttention();
  attention.setFocusDirection(direction);
}

// ============================================
// VISION DATA PARSER (PACKAGE 3) - Buffer-draining version
// Reads ALL buffered messages and only processes the latest
// ============================================
void parseVisionData() {
  if (!ESP32_SERIAL.available()) return;

  static char buffer[128];
  static char latestFace[128];
  bool gotFace = false;
  bool gotNoFace = false;

  // Drain buffer — read all available, keep latest
  while (ESP32_SERIAL.available()) {
    int len = ESP32_SERIAL.readBytesUntil('\n', buffer, sizeof(buffer) - 1);
    if (len == 0) continue;
    buffer[len] = '\0';

    esp32LastMessage = millis();
    esp32MessageCount++;

    if (strncmp(buffer, "FACE:", 5) == 0) {
      memcpy(latestFace, buffer, len + 1);
      gotFace = true;
      gotNoFace = false;  // FACE after NO_FACE overrides
    }
    else if (strncmp(buffer, "NO_FACE", 7) == 0) {
      gotNoFace = true;
      gotFace = false;  // NO_FACE after FACE overrides
    }
    else if (strncmp(buffer, "ESP32_READY", 11) == 0 || strncmp(buffer, "READY", 5) == 0) {
      Serial.println("[VISION] ESP32-S3 connected");
    }
  }

  // Process final state
  if (gotNoFace) {
    if (currentFace.detected) {
      currentFace.detected = false;
      reflexController.faceLost();
      behaviorEngine.stopFaceTracking();
      behaviorEngine.endPersonInteraction();
    }
    return;
  }

  if (!gotFace) return;

  // Parse latest FACE message
  int x, y, vx, vy, w, h, conf;
  unsigned long sequence = 0;
  int parsed = sscanf(latestFace + 5, "%d,%d,%d,%d,%d,%d,%d,%lu",
                      &x, &y, &vx, &vy, &w, &h, &conf, &sequence);

  if (parsed != 8) { esp32ParseErrors++; return; }

  if (x < 0 || x > 240 || y < 0 || y > 240 ||
      w < 0 || w > 240 || h < 0 || h > 240 ||
      conf < 0 || conf > 100) { esp32ParseErrors++; return; }

  // Update face data
  currentFace.x = x;
  currentFace.y = y;
  currentFace.size = w;
  currentFace.confidence = conf;
  currentFace.personID = -1;

  if (w > 80) currentFace.distance = 30;
  else if (w > 60) currentFace.distance = 50;
  else if (w > 40) currentFace.distance = 80;
  else currentFace.distance = 120;

  currentFace.timestamp = millis();
  currentFace.sequence = sequence;
  currentFace.detected = true;
  currentFace.lastSeen = millis();

  reflexController.updateFaceData(x, y, w, currentFace.distance);
  reflexController.updateConfidence(conf);

  freshFaceDataReceived = true;
  lastFaceDataTime = millis();

  handleFaceDetection();
}

// ============================================
// ULTRASONIC SENSOR FUNCTION
// ============================================
int checkUltra(int theEchoPin, int theTrigPin) {
  long duration, distance;
  
  digitalWrite(theTrigPin, LOW);
  delayMicroseconds(2);
  
  digitalWrite(theTrigPin, HIGH);
  delayMicroseconds(10);
  
  digitalWrite(theTrigPin, LOW);
  duration = pulseIn(theEchoPin, HIGH, 30000);
  
  distance = duration / 58.2;
  
  if (distance == 0 || distance > 400) {
    distance = 400;
  }
  
  return distance;
}

// ============================================
// LEGACY MOVETO FUNCTION (for compatibility)
// ============================================
void moveTo(struct headPos faceMotion) {
  MovementStyleParams style = behaviorEngine.getMovementStyle();
  
  servoController.smoothMoveTo(
    faceMotion.baseServoAngle,
    faceMotion.nodServoAngle,
    faceMotion.tiltServoAngle,
    style
  );
}

// ============================================
// SETUP
// ============================================
void setup() {
  // CRITICAL: Initialize Serial FIRST and WAIT
  Serial.begin(115200);
  delay(2000);  // Give Serial time to initialize
  
  Serial.println("\n\n=== SERIAL CONNECTED ===");
  Serial.println("Starting Buddy V15 (Phase 3 - Vision Integration)...");
  Serial.flush();
  delay(100);
  
  // NEW: Initialize ESP32-S3 communication
  Serial.println("[ESP32] Initializing vision communication...");
  ESP32_SERIAL.begin(ESP32_BAUD);
  delay(100);

  // Send ready signal to ESP32-S3
  ESP32_SERIAL.println("TEENSY_READY");
  Serial.println("  ✓ ESP32 v7.2.1 Serial initialized at 921600 baud");
  Serial.println("  ✓ Ready to receive face detection data");
  Serial.println("  ✓ Format: FACE:x,y,vx,vy,w,h,conf,seq");
  delay(100);
  
  // Checkpoint 1
  Serial.println("[1/8] Configuring pins...");
  Serial.flush();
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  pinMode(buzzerPin, OUTPUT);
  Serial.println("  ✓ Pins configured");
  delay(100);
  
  // Checkpoint 2
  Serial.println("[2/8] Attaching servos...");
  Serial.flush();
  baseServo.attach(BASE_SERVO_PIN);
  delay(50);
  nodServo.attach(NOD_SERVO_PIN);
  delay(50);
  tiltServo.attach(TILT_SERVO_PIN);
  delay(50);
  Serial.println("  ✓ Servos attached");
  
  // Checkpoint 3
  Serial.println("[3/8] Moving to safe position...");
  Serial.flush();
  baseServo.write(90);
  delay(100);
  nodServo.write(105);
  delay(100);
  tiltServo.write(90);
  delay(500);
  Serial.println("  ✓ Servos positioned");
  
  // Checkpoint 4
  Serial.println("[4/8] Initializing servo controller...");
  Serial.flush();
  servoController.initialize(90, 105, 90);
  Serial.println("  ✓ Servo controller ready");
  delay(100);
  
  // Checkpoint 5
  Serial.println("[5/8] Linking animation system...");
  Serial.flush();
  behaviorEngine.setServoController(&servoController);
  behaviorEngine.setAnimator(&animator);
  behaviorEngine.setReflexController(&reflexController);
  Serial.println("  ✓ Animation linked");
  Serial.println("  ✓ Reflex controller linked");
  delay(100);
  
  // Checkpoint 6
  Serial.println("[6/8] Initializing behavior engine...");
  Serial.println("  (This may take a moment...)");
  Serial.flush();
  
  behaviorEngine.begin();
  
  Serial.println("  ✓ Behavior engine ready");
  delay(100);

  // Initialize AI Bridge
  aiBridge.init(&behaviorEngine, &servoController, &animator, &reflexController);
  Serial.println("  ✓ AI Bridge initialized (use ! prefix for AI commands)");

  // Checkpoint 7
  Serial.println("[7/8] Performing startup animation...");
  Serial.flush();
  startupAnimation();
  Serial.println("  ✓ Animation complete");
  
  // Checkpoint 8
  Serial.println("[8/8] Final initialization...");
  Serial.flush();
  delay(500);
  
  Serial.println("\n╔════════════════════════════════════╗");
  Serial.println("║    BUDDY V15 IS READY! (PHASE 3)   ║");
  Serial.println("║    Press 'h' for help menu         ║");
  Serial.println("║    Vision system active!           ║");
  Serial.println("╚════════════════════════════════════╝\n");
  Serial.flush();
}

// ============================================
// STARTUP ANIMATION (SPATIAL)
// ============================================
void startupAnimation() {
  BodySchema& bodySchema = behaviorEngine.getBodySchema();
  Emotion& emotion = behaviorEngine.getEmotion();
  Personality& personality = behaviorEngine.getPersonality();
  Needs& needs = behaviorEngine.getNeeds();
  
  Serial.println("\n[STARTUP] Spatial orientation sequence...");
  
  MovementStyleParams style = behaviorEngine.getMovementStyle();
  style.speed = 0.5;  // Slower for startup
  
  // Look left spatially
  Serial.println("  Looking left...");
  ServoAngles left = bodySchema.lookAt(-40, 50, 20);
  servoController.smoothMoveTo(left.base, left.nod, left.tilt, style);
  delay(600);
  
  // Look center
  Serial.println("  Looking center...");
  ServoAngles center = bodySchema.lookAt(0, 50, 20);
  servoController.smoothMoveTo(center.base, center.nod, center.tilt, style);
  delay(600);
  
  // Look right
  Serial.println("  Looking right...");
  ServoAngles right = bodySchema.lookAt(40, 50, 20);
  servoController.smoothMoveTo(right.base, right.nod, right.tilt, style);
  delay(600);
  
  // Look up
  Serial.println("  Looking up...");
  ServoAngles up = bodySchema.lookAt(0, 45, 30);
  servoController.smoothMoveTo(up.base, up.nod, up.tilt, style);
  delay(600);
  
  // Return to neutral forward
  Serial.println("  Returning to neutral...");
  ServoAngles neutral = bodySchema.lookAt(0, 50, 20);
  servoController.smoothMoveTo(neutral.base, neutral.nod, neutral.tilt, style);
  
  Serial.println("✓ Spatial startup complete - Buddy is aware\n");
}

// ============================================
// MAIN LOOP
// ============================================
void loop() {
  unsigned long now = millis();

  // Update at fixed interval (50Hz) - MATCHES TEENSY EXACTLY
  if (now - lastUpdate >= UPDATE_INTERVAL) {
    // ═══════════════════════════════════════════════════════════════
    // CRITICAL: Update timestamp FIRST (matches Teensy architecture)
    // This prevents timing drift from variable work duration
    // ═══════════════════════════════════════════════════════════════
    lastUpdate = now;  // ← UPDATE FIRST, like Teensy!

    // ═══════════════════════════════════════════════════════════════
    // Parse ESP32 data at 50Hz (inside timing check)
    // This matches Teensy's effective rate and prevents runaway
    // ═══════════════════════════════════════════════════════════════
    parseVisionData();

    // ═══════════════════════════════════════════════════════════════
    // PERFORMANCE PROFILING: Measure each section
    // ═══════════════════════════════════════════════════════════════
    unsigned long loopStart = micros();

    // Measure behavior system or tracking mode
    unsigned long behaviorStart = micros();
    unsigned long behaviorTime = 0;
    unsigned long ultrasonicTime = 0;

    // Check if in face tracking only mode
    if (faceTrackingMode) {
      // TRACKING MODE: Only perform face tracking, skip behavior system

      // NEW: Check reflex timeout (disables reflex if no face data)
      reflexController.checkTimeout();

    } else {
      // NORMAL MODE: Full behavior system

      // ═══════════════════════════════════════════════════════════════
      // OPTIMIZATION: Skip ultrasonic during active reflex tracking
      // ═══════════════════════════════════════════════════════════════
      static float lastDistance = 100.0;
      float distance = lastDistance;  // Use cached value by default

      if (!reflexController.isActive()) {
        // Only read ultrasonic when NOT tracking (MAJOR PERFORMANCE GAIN)
        unsigned long ultraStart = micros();
        distance = checkUltra(echoPin, trigPin);
        ultrasonicTime = micros() - ultraStart;
        lastDistance = distance;  // Cache for next iteration
      } else {
        // Use cached distance while tracking (saves 30-60ms per loop!)
        ultrasonicTime = 0;
      }

      // Get current servo positions
      int baseAngle = servoController.getBasePos();
      int nodAngle = servoController.getNodPos();

      // Skip behavior engine during AI looping animations to prevent
      // smoothMoveTo blocking the loop and fighting with directWrite
      if (!aiBridge.isAIAnimating()) {
        // Update behavior engine (this drives everything)
        behaviorEngine.update(distance, baseAngle, nodAngle);
      }
      behaviorTime = micros() - behaviorStart;

      // NEW: Check reflex timeout (disables reflex if no face data)
      reflexController.checkTimeout();
    }

    // ========================================================================
    // CRITICAL FIX: ONLY move servos on FRESH face data
    // ========================================================================
    // ESP32 sends updates at ~8-10Hz (100-120ms)
    // Reflex runs at 50Hz (20ms)
    // Must NOT respond to same face position multiple times!
    //
    // Problem: Without this check, reflex calculates 5-6 times per ESP32 update,
    //          building up momentum and overshooting before realizing face moved
    // Solution: Only calculate/move when we have NEW data
    // ========================================================================

    // Measure reflex calculation
    unsigned long reflexStart = micros();
    unsigned long reflexTime = 0;

    if (reflexController.isActive() && currentFace.detected && freshFaceDataReceived) {
      // ═══════════════════════════════════════════════════════════════
      // FRESH DATA CONFIRMED - safe to calculate and move
      // ═══════════════════════════════════════════════════════════════

      // Clear the flag immediately to prevent re-processing same data
      freshFaceDataReceived = false;

      // Get current servo positions
      int currentBase = servoController.getBasePos();
      int currentNod = servoController.getNodPos();

      // Calculate reflex adjustments using ReflexiveControl layer
      int targetBase, targetNod;
      if (reflexController.calculate(currentBase, currentNod, targetBase, targetNod)) {
        reflexTime = micros() - reflexStart;
        // ════════════════════════════════════════════════════════════
        // ENHANCED FIX: Clamp to limits with smart disable
        // ════════════════════════════════════════════════════════════
        bool wasLimited = false;
        int originalBase = targetBase;
        int originalNod = targetNod;

        // Clamp to safe ranges (soft limits)
        targetBase = constrain(targetBase, 10, 170);
        targetNod = constrain(targetNod, 80, 150);

        // Track if we had to clamp
        if (targetBase != originalBase || targetNod != originalNod) {
          wasLimited = true;
          static unsigned long lastLimitWarning = 0;
          // Throttle warning messages to every 2 seconds
          if (now - lastLimitWarning > 2000) {
            Serial.print("[LIMIT] Clamped: Base ");
            Serial.print(originalBase);
            Serial.print("° → ");
            Serial.print(targetBase);
            Serial.print("°, Nod ");
            Serial.print(originalNod);
            Serial.print("° → ");
            Serial.print(targetNod);
            Serial.println("°");
            lastLimitWarning = now;
          }
        }

        // ALWAYS send command (clamped if necessary) - maintains tracking at limits
        servoController.directWrite(targetBase, targetNod, false);

        // ════════════════════════════════════════════════════════════
        // SMART DISABLE: Only disable if stuck at limit for extended period
        // ════════════════════════════════════════════════════════════
        static int limitCounter = 0;
        static unsigned long firstLimitTime = 0;
        static int lastLimitedBase = 0;
        static int lastLimitedNod = 0;

        if (wasLimited) {
          // First time hitting limit, or hit different limit
          if (limitCounter == 0 || (abs(targetBase - lastLimitedBase) > 5 || abs(targetNod - lastLimitedNod) > 5)) {
            firstLimitTime = now;
            limitCounter = 1;
            lastLimitedBase = targetBase;
            lastLimitedNod = targetNod;
          } else {
            // Same limit position - increment counter
            limitCounter++;
          }

          // Check if stuck at limit for 3+ seconds AND not making progress
          // (150 updates at 20ms = 3 seconds)
          if (limitCounter > 150 && (now - firstLimitTime > 3000)) {
            Serial.println("[LIMIT] Stuck at servo limit for 3s - disabling reflex");
            Serial.print("  Final position: Base=");
            Serial.print(targetBase);
            Serial.print("° Nod=");
            Serial.print(targetNod);
            Serial.println("°");
            reflexController.disable();
            limitCounter = 0;
            firstLimitTime = 0;
          }
        } else {
          // Not at limit - reset counter (target has moved away from limits)
          if (limitCounter > 0) {
            limitCounter = 0;
            firstLimitTime = 0;
          }
        }
      }
    }

    // Check if face data is stale (timeout after 2 seconds)
    if (currentFace.detected && (now - currentFace.lastSeen > 2000)) {
      currentFace.detected = false;
    }

    // ═══════════════════════════════════════════════════════════════
    // PERFORMANCE REPORTING: Print timing summary every 2 seconds
    // ═══════════════════════════════════════════════════════════════
    unsigned long loopTime = micros() - loopStart;

    static unsigned long lastTimingPrint = 0;
    static unsigned long maxLoopTime = 0;
    static unsigned long minLoopTime = 999999;
    static unsigned long sumLoopTime = 0;
    static int loopCount = 0;

    // Track min/max/average
    if (loopTime > maxLoopTime) maxLoopTime = loopTime;
    if (loopTime < minLoopTime) minLoopTime = loopTime;
    sumLoopTime += loopTime;
    loopCount++;

    if (now - lastTimingPrint > 2000) {
      unsigned long avgLoopTime = (loopCount > 0) ? (sumLoopTime / loopCount) : 0;

      Serial.println("\n╔════════════════════════════════════════════════════╗");
      Serial.println("║         PERFORMANCE PROFILE                        ║");
      Serial.println("╚════════════════════════════════════════════════════╝");

      Serial.print("  Total loop time: ");
      Serial.print(loopTime);
      Serial.print(" µs (avg: ");
      Serial.print(avgLoopTime);
      Serial.print(" µs, min: ");
      Serial.print(minLoopTime);
      Serial.print(" µs, max: ");
      Serial.print(maxLoopTime);
      Serial.println(" µs)");

      // NOTE: Vision parse now runs every loop iteration (outside timing)
      // This matches Teensy architecture for continuous serial parsing

      if (ultrasonicTime > 0) {
        Serial.print("  - Ultrasonic: ");
        Serial.print(ultrasonicTime);
        Serial.print(" µs (");
        Serial.print((ultrasonicTime * 100) / loopTime);
        Serial.println("%) ← BLOCKING!");
      }

      if (behaviorTime > 0) {
        Serial.print("  - Behavior system: ");
        Serial.print(behaviorTime);
        Serial.print(" µs (");
        Serial.print((behaviorTime * 100) / loopTime);
        Serial.println("%)");
      }

      if (reflexTime > 0) {
        Serial.print("  - Reflex calc: ");
        Serial.print(reflexTime);
        Serial.print(" µs (");
        Serial.print((reflexTime * 100) / loopTime);
        Serial.println("%)");
      }

      Serial.print("  Loop frequency: ");
      if (avgLoopTime > 0) {
        Serial.print(1000000.0 / avgLoopTime);
      } else {
        Serial.print("N/A");
      }
      Serial.println(" Hz");

      Serial.println("════════════════════════════════════════════════════\n");

      // ═══════════════════════════════════════════════════════════════
      // REFLEX MODE STATUS - Verify normal behaviors execute when OFF
      // ═══════════════════════════════════════════════════════════════
      Serial.println("╔════════════════════════════════════════════════════╗");
      Serial.println("║         REFLEX MODE STATUS                         ║");
      Serial.println("╚════════════════════════════════════════════════════╝");

      bool reflexActive = reflexController.isActive();
      bool faceTracking = behaviorEngine.getIsTrackingFace();

      Serial.print("  Reflex active: ");
      if (reflexActive) {
        Serial.println("YES - tracking mode");
        Serial.println("  → Behavior system should be BLOCKED");
        Serial.println("  → ONLY reflex controls servos");
      } else {
        Serial.println("NO - normal mode");
        Serial.println("  → Behavior system should be ACTIVE");
        Serial.println("  → Full consciousness running");
      }

      Serial.print("\n  Face tracking: ");
      Serial.println(faceTracking ? "YES" : "NO");

      Serial.print("  Current behavior: ");
      Serial.println(behaviorEngine.getCurrentBehavior());

      if (!reflexActive) {
        Serial.println("\n  ℹ EXPECT: Behavior should change every 5-10s");
        Serial.println("  ℹ EXPECT: Regular servo movements visible");
      }

      Serial.println("════════════════════════════════════════════════════\n");

      // Reset statistics
      lastTimingPrint = now;
      maxLoopTime = 0;
      minLoopTime = 999999;
      sumLoopTime = 0;
      loopCount = 0;
    }

    // ═══════════════════════════════════════════════════════════════
    // REFLEX ON-OFF SWITCH VERIFICATION: Track mode transitions
    // ═══════════════════════════════════════════════════════════════
    static bool lastReflexState = false;
    bool currentReflexState = reflexController.isActive();

    // Log state changes
    if (currentReflexState != lastReflexState) {
      Serial.println("\n╔═══════════════════════════════════════╗");
      if (currentReflexState) {
        Serial.println("║  REFLEX MODE: ON (TRACKING)           ║");
        Serial.println("║  → Behavior system BLOCKED            ║");
        Serial.println("║  → ONLY reflex controls servos        ║");
      } else {
        Serial.println("║  REFLEX MODE: OFF (NORMAL)            ║");
        Serial.println("║  → Behavior system ACTIVE             ║");
        Serial.println("║  → Full consciousness running         ║");
      }
      Serial.println("╚═══════════════════════════════════════╝\n");
      lastReflexState = currentReflexState;
    }

    // NOTE: lastUpdate already set at START of timing block (line 520)
    // This matches Teensy architecture and prevents drift
  }

  // Periodic diagnostics (every 5 minutes)
  if (now - lastDiagnostics >= DIAGNOSTICS_INTERVAL) {
    behaviorEngine.printFullDiagnostics();
    lastDiagnostics = now;
  }
  
  // Save state periodically (every 30 minutes to reduce EEPROM wear)
  static unsigned long lastSave = 0;
  if (now - lastSave > 1800000) {  // 30 minutes = 1800000ms
    behaviorEngine.saveState();
    lastSave = now;
  }

  // AI Bridge: Update looping animations (THINKING/SPEAKING) at 20Hz
  aiBridge.updateLoopingAnimation();

  // AI Bridge: Send streaming state if enabled (every 500ms)
  aiBridge.updateStreaming();

  // ═══════════════════════════════════════════════════════════════
  // LOOP RATE LIMITING: Match Teensy's natural throttling
  // ═══════════════════════════════════════════════════════════════
  // Teensy: Serial blocking provides natural ~few hundred Hz throttling
  // Buddy: Use delay(5) to match that behavior
  //
  // delay(5) = ~200Hz loop rate:
  //   - Still 4x faster than 50Hz update rate (plenty of margin)
  //   - Matches Teensy's natural throttling behavior
  //   - Prevents runaway behavior
  //   - Stable and controlled like Teensy
  //
  // This exactly matches Teensy's stability characteristics
  // ═══════════════════════════════════════════════════════════════
  delay(5);  // 5ms = ~200Hz loop rate (matches Teensy's natural rate)
}

// ============================================
// SERIAL COMMANDS
// ============================================
void serialEvent() {
  if (Serial.available()) {
    char cmd = Serial.read();
    
    switch(cmd) {
      case 'd':
      case 'D':
        behaviorEngine.printFullDiagnostics();
        break;
        
      case 's':
      case 'S':
        behaviorEngine.saveState();
        Serial.println("State saved!");
        break;

      case 'n':
      case 'N':
        {
          // Return to neutral spatial position
          BodySchema& bodySchema = behaviorEngine.getBodySchema();
          ServoAngles neutral = bodySchema.lookAt(0, 50, 20);
          MovementStyleParams style = behaviorEngine.getMovementStyle();
          servoController.smoothMoveTo(neutral.base, neutral.nod, neutral.tilt, style);
          Serial.println("Returned to spatial neutral");
        }
        break;
        
      case 't':
      case 'T':
        {
          // Test spatial looking
          BodySchema& bodySchema = behaviorEngine.getBodySchema();
          
          Serial.println("\n[TEST] Spatial targeting test");
          
          // Look at 4 spatial points
          float points[][3] = {
            {-30, 40, 18},  // Left
            {30, 40, 18},   // Right
            {0, 50, 10},    // Low center
            {0, 50, 25}     // High center
          };
          
          MovementStyleParams style = behaviorEngine.getMovementStyle();
          
          for (int i = 0; i < 4; i++) {
            Serial.print("  Target ");
            Serial.print(i+1);
            Serial.print(": (");
            Serial.print(points[i][0], 0);
            Serial.print(", ");
            Serial.print(points[i][1], 0);
            Serial.print(", ");
            Serial.print(points[i][2], 0);
            Serial.println(")");
            
            ServoAngles angles = bodySchema.lookAt(points[i][0], points[i][1], points[i][2]);
            servoController.smoothMoveTo(angles.base, angles.nod, angles.tilt, style);
            delay(1000);
          }
          
          Serial.println("✓ Spatial test complete");
        }
        break;
        
      case 'p':
      case 'P':
        {
          Emotion& emotion = behaviorEngine.getEmotion();
          Personality& personality = behaviorEngine.getPersonality();
          Needs& needs = behaviorEngine.getNeeds();
          animator.playfulBounce(emotion, personality, needs);
          Serial.println("Playful bounce animation");
        }
        break;
        
      case 'k':
      case 'K':
        {
          // Test body schema kinematics
          BodySchema& bodySchema = behaviorEngine.getBodySchema();
          bodySchema.testKinematics();
        }
        break;
        
      case 'f':  // NEW: Test face detection
      case 'F':
        {
          Serial.println("\n[TEST] Simulating face detection...");

          // Inject fake face data
          currentFace.detected = true;
          currentFace.x = 140;  // Slightly right
          currentFace.y = 120;  // Center height
          currentFace.size = 60;  // Medium size
          currentFace.confidence = 85;
          currentFace.lastSeen = millis();

          handleFaceDetection();

          Serial.println("  ✓ Face simulation complete");
          Serial.println("  Press 'd' to see social need increase");
        }
        break;

      case 'e':  // NEW: ESP32 communication health
      case 'E':
        {
          Serial.println("\n╔═══ ESP32 COMMUNICATION STATUS ═══╗");
          unsigned long now = millis();
          unsigned long timeSinceMsg = now - esp32LastMessage;

          Serial.print("Messages received: ");
          Serial.println(esp32MessageCount);

          Serial.print("Parse errors: ");
          Serial.println(esp32ParseErrors);

          if (esp32MessageCount > 0) {
            Serial.print("Error rate: ");
            Serial.print((esp32ParseErrors * 100) / esp32MessageCount);
            Serial.println("%");
          }

          Serial.print("Last message: ");
          if (esp32LastMessage == 0) {
            Serial.println("NEVER");
          } else {
            Serial.print(timeSinceMsg / 1000);
            Serial.println("s ago");
          }

          Serial.print("Connection: ");
          if (timeSinceMsg > ESP32_TIMEOUT) {
            Serial.println("TIMEOUT - Check wiring!");
          } else if (timeSinceMsg > 2000) {
            Serial.println("SLOW - May be idle");
          } else {
            Serial.println("ACTIVE");
          }

          Serial.println("╚═══════════════════════════════════╝\n");
        }
        break;

      case 'x':  // NEW: Debug face tracking mode
      case 'X':
        behaviorEngine.toggleDebugFaceTracking();
        break;

      case 'r':  // NEW: Reflex/tracking diagnostics
      case 'R':
        printTrackingDiagnostics();
        break;

      case 'a':  // NEW: Auto face tracking mode toggle
      case 'A':
        faceTrackingMode = !faceTrackingMode;
        if (faceTrackingMode) {
          Serial.println("\n[TRACKING MODE] Auto face tracking ENABLED");
          Serial.println("  Buddy will now only perform face tracking");
          Serial.println("  Press 'a' again to return to normal behaviors\n");
          // Enable reflex controller
          if (currentFace.detected) {
            reflexController.enable();
          }
        } else {
          Serial.println("\n[TRACKING MODE] Auto face tracking DISABLED");
          Serial.println("  Returning to normal behavior system\n");
        }
        break;

      case 'h':
      case 'H':
        printHelp();
        break;

      case '!':
        {
          // AI Bridge command - read rest of line with short timeout
          unsigned long savedTimeout = Serial.getTimeout();
          Serial.setTimeout(100);
          String cmdLine = Serial.readStringUntil('\n');
          Serial.setTimeout(savedTimeout);
          cmdLine.trim();
          aiBridge.handleCommand(cmdLine.c_str());
        }
        break;

      default:
        Serial.println("Unknown command. Press 'h' for help.");
        break;
    }
  }
}

void printTrackingDiagnostics() {
  Serial.println("\n╔════════════════════════════════════════════════════╗");
  Serial.println("║      FACE TRACKING DIAGNOSTICS                     ║");
  Serial.println("╚════════════════════════════════════════════════════╝");

  unsigned long now = millis();

  // Face detection status
  Serial.println("\n[FACE DETECTION]");
  if (currentFace.detected) {
    Serial.print("  Status: ACTIVE (Person ID ");
    Serial.print(currentFace.personID);
    Serial.println(")");
    Serial.print("  Position: (");
    Serial.print(currentFace.x);
    Serial.print(", ");
    Serial.print(currentFace.y);
    Serial.println(")");
    Serial.print("  Size: ");
    Serial.print(currentFace.size);
    Serial.print("px  Distance: ");
    Serial.print(currentFace.distance);
    Serial.println("cm");
    Serial.print("  Confidence: ");
    Serial.print(currentFace.confidence);
    Serial.println("%");

    if (currentFace.sequence > 0) {
      Serial.print("  Message seq: ");
      Serial.print(currentFace.sequence);
      Serial.print("  ESP32 time: ");
      Serial.print(currentFace.timestamp);
      Serial.println("ms");
    }

    Serial.print("  Last update: ");
    Serial.print(now - currentFace.lastSeen);
    Serial.println("ms ago");
  } else {
    Serial.println("  Status: NO FACE DETECTED");
    if (currentFace.lastSeen > 0) {
      Serial.print("  Last seen: ");
      Serial.print((now - currentFace.lastSeen) / 1000);
      Serial.println("s ago");
    }
  }

  // Reflex controller status
  Serial.println("\n[REFLEX CONTROLLER]");
  const ReflexState& reflexState = reflexController.getState();

  Serial.print("  Active: ");
  Serial.println(reflexState.active ? "YES" : "NO");

  if (reflexState.dataIsStale) {
    Serial.println("  ⚠ WARNING: STALE DATA DETECTED!");
    Serial.print("    Coords stuck at (");
    Serial.print(reflexState.prevFaceX);
    Serial.print(",");
    Serial.print(reflexState.prevFaceY);
    Serial.println(")");
    Serial.print("    Stale count: ");
    Serial.println(reflexState.staleDataCount);
  }

  if (reflexState.active || reflexState.lastFaceTime > 0) {
    Serial.print("  Face position: (");
    Serial.print(reflexState.faceX);
    Serial.print(", ");
    Serial.print(reflexState.faceY);
    Serial.println(")");

    Serial.print("  Error: (");
    Serial.print(reflexState.errorX);
    Serial.print(", ");
    Serial.print(reflexState.errorY);
    Serial.print(")px  Magnitude: ");
    Serial.print(reflexState.errorMagnitude, 1);
    Serial.println("px");

    Serial.print("  Servo targets: Base=");
    Serial.print(reflexState.targetBase);
    Serial.print("° Nod=");
    Serial.print(reflexState.targetNod);
    Serial.println("°");

    Serial.print("  Last adjustment: Base");
    Serial.print(reflexState.adjustBase >= 0 ? "+" : "");
    Serial.print(reflexState.adjustBase);
    Serial.print("° Nod");
    Serial.print(reflexState.adjustNod >= 0 ? "+" : "");
    Serial.print(reflexState.adjustNod);
    Serial.println("°");

    Serial.print("  Gain: ");
    Serial.print(reflexState.currentGain, 1);
    Serial.print("  Quality: ");
    Serial.print(reflexState.trackingQuality * 100, 0);
    Serial.println("%");

    Serial.print("  Updates: ");
    Serial.print(reflexState.updateCount);
    Serial.print("  Settled: ");
    Serial.println(reflexState.isSettled ? "YES" : "NO");
  }

  // Current servo positions
  Serial.println("\n[SERVO POSITIONS]");
  int base, nod, tilt;
  servoController.getPosition(base, nod, tilt);
  Serial.print("  Base: ");
  Serial.print(base);
  Serial.print("°  Nod: ");
  Serial.print(nod);
  Serial.print("°  Tilt: ");
  Serial.print(tilt);
  Serial.println("°");

  // Servo limit warnings
  if (base <= 15) {
    Serial.println("  ⚠ WARNING: Base servo near LEFT limit (10°)");
  } else if (base >= 165) {
    Serial.println("  ⚠ WARNING: Base servo near RIGHT limit (170°)");
  }

  if (nod <= 85) {
    Serial.println("  ⚠ WARNING: Nod servo near DOWN limit (80°)");
  } else if (nod >= 145) {
    Serial.println("  ⚠ WARNING: Nod servo near UP limit (150°)");
  }

  // ESP32 communication
  Serial.println("\n[ESP32 COMMUNICATION]");
  Serial.print("  Messages received: ");
  Serial.println(esp32MessageCount);
  Serial.print("  Parse errors: ");
  Serial.print(esp32ParseErrors);
  if (esp32MessageCount > 0) {
    Serial.print(" (");
    Serial.print((esp32ParseErrors * 100) / esp32MessageCount);
    Serial.println("%)");
  } else {
    Serial.println();
  }

  if (esp32LastMessage > 0) {
    Serial.print("  Last message: ");
    Serial.print(now - esp32LastMessage);
    Serial.println("ms ago");
  }

  Serial.println("\n════════════════════════════════════════════════════\n");
  Serial.println("TIP: Press 'r' again to refresh, 'v' for verbose mode");
  Serial.println();
}

void printHelp() {
  Serial.println("\n╔════════════════════════════════════╗");
  Serial.println("║    BUDDY V15 ROBOT COMMANDS        ║");
  Serial.println("╚════════════════════════════════════╝");
  Serial.println("DIAGNOSTICS:");
  Serial.println("  d/D - Print full diagnostics now");
  Serial.println("  e/E - Check ESP32 communication health");
  Serial.println("  r/R - Show tracking diagnostics");
  Serial.println("        (Face position, reflex state)");
  Serial.println("");
  Serial.println("STATE:");
  Serial.println("  s/S - Save state to EEPROM now");
  Serial.println("        (Auto-saves every 30 min)");
  Serial.println("");
  Serial.println("MOVEMENT:");
  Serial.println("  n/N - Return to spatial neutral");
  Serial.println("  t/T - Test spatial targeting");
  Serial.println("  k/K - Test body schema kinematics");
  Serial.println("  p/P - Test playful bounce animation");
  Serial.println("");
  Serial.println("VISION:");
  Serial.println("  f/F - Test face detection (simulate)");
  Serial.println("  x/X - Toggle DEBUG face tracking mode");
  Serial.println("        (Pure tracking, no behaviors)");
  Serial.println("  a/A - Toggle AUTO face tracking mode");
  Serial.println("        (Tracking only, no behavior system)");
  Serial.println("");
  Serial.println("  h/H - Show this help menu");
  Serial.println("");
  Serial.println("AI BRIDGE (prefix !):");
  Serial.println("  !QUERY            - Get state JSON");
  Serial.println("  !LOOK:base,nod    - Move servos");
  Serial.println("  !ATTENTION:dir    - Look direction");
  Serial.println("  !SATISFY:need,amt - Satisfy need");
  Serial.println("  !PRESENCE         - Detect human");
  Serial.println("  !EXPRESS:emotion  - Express emotion");
  Serial.println("  !NOD:count        - Nod yes");
  Serial.println("  !SHAKE:count      - Shake no");
  Serial.println("  !LISTENING        - Attentive pose");
  Serial.println("  !THINKING         - Pondering loop");
  Serial.println("  !STOP_THINKING    - Stop pondering");
  Serial.println("  !SPEAKING         - Speaking loop");
  Serial.println("  !STOP_SPEAKING    - Stop speaking");
  Serial.println("  !ACKNOWLEDGE      - Quick nod");
  Serial.println("  !CELEBRATE        - Happy bounce");
  Serial.println("  !IDLE             - Return to normal");
  Serial.println("  !STREAM:on/off    - Toggle streaming");
  Serial.println("════════════════════════════════════\n");
}

// ============================================
// EMERGENCY STOP
// ============================================
void emergencyStop() {
  Serial.println("\n[EMERGENCY STOP]");
  
  baseServo.detach();
  nodServo.detach();
  tiltServo.detach();
  
  noTone(buzzerPin);
  
  behaviorEngine.saveState();
  
  Serial.println("All systems halted. Reset to restart.");
  
  while(1) {
    delay(1000);
  }
}

/*
 * ============================================
 * USAGE NOTES - V15 PHASE 3
 * ============================================
 * 
 * NEW IN PHASE 3:
 * - ESP32-S3 face detection integrated
 * - Serial communication active (Serial1)
 * - Social behaviors triggered by vision
 * - Spatial memory tracks people
 * - Real-time face tracking @ 93% accuracy
 * 
 * VISION SYSTEM:
 * - Hardware: ESP32-S3 CAM (v7.2.1) connected via Serial1
 * - Pins: Teensy 0(RX),1(TX) ↔ ESP32 43(TX),44(RX)
 * - Baud: 921600 (HIGH SPEED)
 * - Format: FACE:x,y,vx,vy,w,h,conf,seq
 * - Update Rate: 50Hz output from ESP32
 * - ReflexiveControl v6.0: AdaptivePID + state machine tracking
 * 
 * TESTING:
 * - Press 'f' to simulate face detection
 * - Press 'd' to see social need increase
 * - Wave at camera to test real detection
 * - Watch for SOCIAL_ENGAGE behavior
 * 
 * WIRING CHECK:
 * - Common ground between boards (CRITICAL!)
 * - TX→RX crossover (transmit to receive)
 * - Both boards powered via USB
 * 
 * NEXT STEPS:
 * - Test with real face detection
 * - Tune confidence thresholds
 * - Observe behavioral emergence
 * - Package 4: Enhanced behavioral intelligence
 * 
 * ============================================
 */