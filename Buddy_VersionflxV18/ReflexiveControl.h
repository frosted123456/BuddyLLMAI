/**
 * ReflexiveControl.h - v6.0 (Based on Teensy v5.4)
 *
 * Low-level reflexive tracking layer for Buddy Phase-3
 * Rewritten to use proven Teensy v5.4 continuous confidence control
 *
 * NEW IN v6.0 (from Teensy v5.4):
 *   - AdaptivePID controller (4 tuning sets based on error)
 *   - State machine (LOST → ACQUIRE → TRACK)
 *   - Gentle trajectory for return-to-center
 *   - Confidence-modulated control (0-100 continuous)
 *   - Adaptive deadband based on confidence (6-14px)
 *   - Velocity-based predictive tracking
 *   - Smooth motion scaling
 *   - Oscillation detection
 *
 * Hardware Context:
 * - ESP32-CAM mounted on nodServo (10cm arm on baseServo)
 * - Camera rotates with base, creating parallax effects
 * - Servo ranges: base(10-170°), nod(80-150°), tilt(20-150°)
 */

#ifndef REFLEXIVE_CONTROL_H
#define REFLEXIVE_CONTROL_H

#include <Arduino.h>

// ============================================================================
// CONFIGURATION CONSTANTS
// ============================================================================

// Camera geometry
#define CAMERA_CENTER_X 120
#define CAMERA_CENTER_Y 120
#define CAMERA_FRAME_WIDTH 240
#define CAMERA_FRAME_HEIGHT 240

// Servo ranges (matching Teensy v5.4)
#define BASE_MIN 10
#define BASE_MAX 170
#define BASE_CENTER 90

#define NOD_MIN 80
#define NOD_MAX 150
#define NOD_CENTER 115

#define TILT_MIN 20
#define TILT_MAX 150
#define TILT_CENTER 85

// Control loop timing
#define REFLEX_UPDATE_RATE_MS 20        // 50Hz control loop (matching v5.4)
#define SERVO_FEEDBACK_RATE_MS 20       // 50Hz servo feedback to ESP32

// State machine thresholds
#define ACQUIRE_THRESHOLD 20             // Pixels from center to consider "acquired"
#define FRAMES_TO_ACQUIRE 1              // Frames needed to enter ACQUIRE state
#define FRAMES_TO_TRACK 2                // Frames needed to enter TRACK state
#define FRAMES_TO_LOST 10                // Frames without face to enter LOST state

// Trajectory parameters
#define RETURN_TO_CENTER_TIMEOUT_MS 1500 // Time before returning to center
#define BLIND_IGNORE_FRAMES 5            // Frames to ignore during blind return
#define SETTLING_FRAMES 10               // Frames for gentle settling
#define SETTLING_GAIN_SCALE 0.3          // Reduced gain during settling

// Velocity and smoothing
// ═══════════════════════════════════════════════════════════════
// CRITICAL TUNING: Reduced to prevent overshoot/runaway
// Old: 12.0 degrees/frame was too aggressive at consistent 50Hz
// New: 6.0 degrees/frame = smoother, prevents chasing face out of frame
// ═══════════════════════════════════════════════════════════════
#define MAX_VELOCITY_PER_FRAME 6.0       // Max degrees per frame (was 12.0)
#define SMOOTHING_FACTOR 0.5             // Motion smoothing (was 0.65, reduced for stability)
#define REFERENCE_FACE_WIDTH 55.0        // Reference face size for depth scaling

// Stale data detection (PRESERVED from original - critical for Pi)
#define STALE_DATA_THRESHOLD 3           // Pixel change threshold
#define STALE_DATA_TIMEOUT_MS 300        // Max time without coordinate change
#define STALE_DATA_MAX_COUNT 5           // Max consecutive stale updates


// ============================================================================
// ADAPTIVE PID CONTROLLER (from Teensy v5.4)
// ============================================================================

class AdaptivePID {
private:
  float Kp, Ki, Kd;
  float integral;
  float prev_error;
  float max_integral;

  // ═══════════════════════════════════════════════════════════════
  // CRITICAL TUNING: Reduced PID gains to prevent overshoot/runaway
  // Old gains were too aggressive at consistent 50Hz update rate
  // New gains: Reduced by ~40% for stability
  // ═══════════════════════════════════════════════════════════════
  // Four tuning sets for different error magnitudes
  const float LARGE_ERROR_KP = 0.11;      // was 0.18
  const float LARGE_ERROR_KD = 0.004;     // was 0.006

  const float MEDIUM_ERROR_KP = 0.09;     // was 0.14
  const float MEDIUM_ERROR_KD = 0.003;    // was 0.005

  const float BALANCED_KP = 0.07;         // was 0.11
  const float BALANCED_KD = 0.0025;       // was 0.004

  const float PRECISE_KP = 0.05;          // was 0.08
  const float PRECISE_KD = 0.0015;        // was 0.0025

public:
  AdaptivePID() {
    Kp = BALANCED_KP;
    Ki = 0.012;
    Kd = BALANCED_KD;
    max_integral = 15.0;
    reset();
  }

  void reset() {
    integral = 0.0;
    prev_error = 0.0;
  }

  void updateGains(float error, float motionScale = 1.0) {
    float absError = abs(error);

    // Select PID gains based on error magnitude
    if (absError > 50) {
      Kp = LARGE_ERROR_KP;
      Kd = LARGE_ERROR_KD;
    } else if (absError > 30) {
      Kp = MEDIUM_ERROR_KP;
      Kd = MEDIUM_ERROR_KD;
    } else if (absError > 15) {
      Kp = BALANCED_KP;
      Kd = BALANCED_KD;
    } else {
      Kp = PRECISE_KP;
      Kd = PRECISE_KD;
    }

    // Apply motion scaling
    Kp *= motionScale;
    Kd *= motionScale;
  }

  float update(float error, float dt) {
    float derivative = (error - prev_error) / dt;

    integral += Ki * error * dt;
    integral = constrain(integral, -max_integral, max_integral);

    float output = Kp * error + integral + Kd * derivative;

    prev_error = error;

    return output;
  }

  float getKp() { return Kp; }
};


// ============================================================================
// GENTLE TRAJECTORY (from Teensy v5.4)
// ============================================================================

class GentleTrajectory {
private:
  bool active;
  float startPan, startTilt;
  float targetPan, targetTilt;
  float currentStep;
  float totalSteps;

public:
  GentleTrajectory() {
    active = false;
    currentStep = 0;
    totalSteps = 0;
  }

  void planReturnToCenter(float fromPan, float fromTilt) {
    startPan = fromPan;
    startTilt = fromTilt;
    targetPan = BASE_CENTER;
    targetTilt = NOD_CENTER;

    // Calculate smooth trajectory duration based on distance
    float distance = sqrt(pow(targetPan - fromPan, 2) + pow(targetTilt - fromTilt, 2));
    float durationSeconds = distance / 60.0;
    durationSeconds = constrain(durationSeconds, 0.3, 1.5);

    totalSteps = durationSeconds * 50;  // 50Hz update rate
    currentStep = 0;
    active = true;
  }

  bool getNextPosition(float& pan, float& tilt) {
    if (!active) return false;

    if (currentStep >= totalSteps) {
      active = false;
      return false;
    }

    // Ease-in-out curve for smooth motion
    float t = currentStep / totalSteps;
    float smoothT = (t < 0.5) ? 2 * t * t : 1 - pow(-2 * t + 2, 2) / 2;

    pan = startPan + (targetPan - startPan) * smoothT;
    tilt = startTilt + (targetTilt - startTilt) * smoothT;

    currentStep++;
    return true;
  }

  bool isActive() { return active; }
  void cancel() { active = false; }
};


// ============================================================================
// STATE MACHINES (from Teensy v5.4)
// ============================================================================

enum ControlState {
  LOST,      // No face detected
  ACQUIRE,   // Face found, moving to center
  TRACK      // Face centered, smooth tracking
};

enum BlindState {
  NORMAL,           // Normal operation
  BLIND_MOVING,     // Ignoring face data during return-to-center
  GENTLE_SETTLING   // Reduced gain after blind movement
};


// ============================================================================
// REFLEX STATE STRUCTURE
// ============================================================================

struct ReflexState {
  // Activation state
  bool active;
  bool shouldBeActive;

  // State machines
  ControlState controlState;
  BlindState blindState;

  // Face tracking data
  int faceX;
  int faceY;
  int faceVX;                     // Velocity X (derived)
  int faceVY;                     // Velocity Y (derived)
  int faceSize;
  int faceConfidence;             // 0-100 continuous
  int faceDistance;
  unsigned long lastFaceTime;

  // Stale data detection (PRESERVED from original)
  int prevFaceX;
  int prevFaceY;
  unsigned long lastChangeTime;
  int staleDataCount;
  bool dataIsStale;

  // Frame counters
  int framesTracked;
  int framesLost;
  int blindFrameCounter;
  int oscillationCount;

  // Servo targets
  float panAngle;
  float tiltAngle;
  int targetBase;
  int targetNod;

  // Tracking metrics
  float trackingQuality;
  float errorMagnitude;
  float prevErrorMagnitude;
  bool isSettled;

  // Debug
  int updateCount;
  int errorX;
  int errorY;
  int adjustBase;         // Last base adjustment (for diagnostics)
  int adjustNod;          // Last nod adjustment (for diagnostics)
  float currentGain;      // Current PID gain (for diagnostics)
};


// ============================================================================
// REFLEXIVE CONTROL CLASS (v6.0 - Based on Teensy v5.4)
// ============================================================================

class ReflexiveControl {
private:
  ReflexState state;

  AdaptivePID panPID;
  AdaptivePID tiltPID;
  GentleTrajectory trajectory;

  unsigned long lastUpdateTime;
  unsigned long lastServoSendTime;

  bool isReturningToCenter;

  const float CONTROL_DT = 0.02;  // 50Hz = 20ms = 0.02s

  // For velocity calculation (derived from position)
  int lastFaceX;
  int lastFaceY;
  unsigned long lastVelocityTime;

public:

  // ========================================================================
  // CONSTRUCTOR & INITIALIZATION
  // ========================================================================

  ReflexiveControl() {
    reset();
  }

  void reset() {
    state.active = false;
    state.shouldBeActive = false;
    state.controlState = LOST;
    state.blindState = NORMAL;

    state.faceX = CAMERA_CENTER_X;
    state.faceY = CAMERA_CENTER_Y;
    state.faceVX = 0;
    state.faceVY = 0;
    state.faceSize = 0;
    state.faceConfidence = 0;
    state.faceDistance = 100;
    state.lastFaceTime = 0;

    state.prevFaceX = CAMERA_CENTER_X;
    state.prevFaceY = CAMERA_CENTER_Y;
    state.lastChangeTime = 0;
    state.staleDataCount = 0;
    state.dataIsStale = false;

    state.framesTracked = 0;
    state.framesLost = 0;
    state.blindFrameCounter = 0;
    state.oscillationCount = 0;

    state.panAngle = BASE_CENTER;
    state.tiltAngle = NOD_CENTER;
    state.targetBase = BASE_CENTER;
    state.targetNod = NOD_CENTER;

    state.trackingQuality = 0.0f;
    state.errorMagnitude = 0.0f;
    state.prevErrorMagnitude = 0.0f;
    state.isSettled = false;

    state.updateCount = 0;
    state.errorX = 0;
    state.errorY = 0;
    state.adjustBase = 0;
    state.adjustNod = 0;
    state.currentGain = 0.11;  // Start with BALANCED_KP

    lastUpdateTime = 0;
    lastServoSendTime = 0;
    isReturningToCenter = false;

    lastFaceX = CAMERA_CENTER_X;
    lastFaceY = CAMERA_CENTER_Y;
    lastVelocityTime = 0;

    panPID.reset();
    tiltPID.reset();
  }


  // ========================================================================
  // REFLEX ACTIVATION CONTROL
  // ========================================================================

  void enable() {
    state.shouldBeActive = true;
    if (!state.active) {
      state.active = true;
    }
  }

  void disable() {
    state.shouldBeActive = false;
    if (state.active) {
      state.active = false;
      state.isSettled = false;
    }
  }

  void checkTimeout() {
    if (!state.active) return;

    // ═══════════════════════════════════════════════════════════════
    // OPTIMIZATION: Only check timeout periodically, not every call
    // ═══════════════════════════════════════════════════════════════
    static unsigned long lastCheck = 0;
    unsigned long now = millis();

    // Only check every 500ms (not every loop iteration)
    if (now - lastCheck < 500) {
      return;
    }
    lastCheck = now;

    // Original timeout logic
    if (state.lastFaceTime > 0) {
      unsigned long timeSinceFace = now - state.lastFaceTime;
      if (timeSinceFace > 2000) {  // 2000ms (2 second) timeout - allows tolerance during tracking
        state.active = false;
      }
    }
  }


  // ========================================================================
  // FACE DATA INPUT
  // ========================================================================

  /**
   * Update with new face detection data from ESP32
   * Signature preserved for compatibility with existing code
   */
  void updateFaceData(int x, int y, int size, int distance) {
    unsigned long now = millis();

    // Constrain inputs
    x = constrain(x, 0, CAMERA_FRAME_WIDTH);
    y = constrain(y, 0, CAMERA_FRAME_HEIGHT);

    // ========================================================================
    // STALE DATA DETECTION (PRESERVED from original - critical for Pi)
    // ========================================================================

    int deltaX = abs(x - state.prevFaceX);
    int deltaY = abs(y - state.prevFaceY);
    int totalChange = deltaX + deltaY;

    if (totalChange >= STALE_DATA_THRESHOLD) {
      // Fresh data - coordinates changed
      state.prevFaceX = x;
      state.prevFaceY = y;
      state.lastChangeTime = now;
      state.staleDataCount = 0;
      state.dataIsStale = false;
    } else {
      // Potentially stale data
      state.staleDataCount++;
      unsigned long timeSinceChange = now - state.lastChangeTime;

      if (timeSinceChange > STALE_DATA_TIMEOUT_MS ||
          state.staleDataCount > STALE_DATA_MAX_COUNT) {

        state.dataIsStale = true;

        if (state.active) {
          state.active = false;
        }
        return;  // Don't update with stale values
      }
    }

    // ========================================================================
    // CALCULATE VELOCITY (derived from position changes)
    // ========================================================================

    if (lastVelocityTime > 0) {
      float dt = (now - lastVelocityTime) / 1000.0;  // seconds
      if (dt > 0.001 && dt < 0.5) {  // Reasonable time delta
        state.faceVX = (int)((x - lastFaceX) / dt);
        state.faceVY = (int)((y - lastFaceY) / dt);

        // Limit velocity to reasonable values
        state.faceVX = constrain(state.faceVX, -200, 200);
        state.faceVY = constrain(state.faceVY, -200, 200);
      }
    }

    lastFaceX = x;
    lastFaceY = y;
    lastVelocityTime = now;

    // ========================================================================
    // STORE FACE DATA
    // ========================================================================

    state.faceX = x;
    state.faceY = y;
    state.faceSize = size;
    state.faceDistance = distance;
    state.lastFaceTime = now;

    // Note: confidence will be set separately via updateConfidence()
    // or default to 100 for compatibility
    if (state.faceConfidence == 0) {
      state.faceConfidence = 100;  // Default for systems not sending confidence
    }

    // Re-enable if should be active and data is fresh
    if (state.shouldBeActive && !state.active && !state.dataIsStale) {
      state.active = true;
    }
  }

  /**
   * NEW: Set confidence value (0-100)
   * Call this after updateFaceData if confidence is available separately
   */
  void updateConfidence(int confidence) {
    state.faceConfidence = constrain(confidence, 0, 100);
  }

  void faceLost() {
    if (state.active) {
      state.active = false;
      state.isSettled = false;
    }
  }


  // ========================================================================
  // REFLEX COMPUTATION (CORE ALGORITHM from Teensy v5.4)
  // ========================================================================

  /**
   * Calculate reflexive servo adjustments
   * Interface preserved for compatibility with existing code
   */
  bool calculate(int currentBase, int currentNod, int& baseOut, int& nodOut) {
    unsigned long now = millis();

    // Throttle update rate (50Hz)
    if (now - lastUpdateTime < REFLEX_UPDATE_RATE_MS) {
      baseOut = state.targetBase;
      nodOut = state.targetNod;
      return state.active;
    }
    lastUpdateTime = now;

    // Update current angles for trajectory planning
    state.panAngle = currentBase;
    state.tiltAngle = currentNod;

    // ═══════════════════════════════════════════════
    // BLIND STATE MACHINE
    // ═══════════════════════════════════════════════

    if (state.blindState != NORMAL) {
      state.blindFrameCounter++;

      if (state.blindState == BLIND_MOVING) {
        if (state.blindFrameCounter <= BLIND_IGNORE_FRAMES) {
          // Continue trajectory, ignore face data
          float trajPan, trajTilt;
          if (trajectory.getNextPosition(trajPan, trajTilt)) {
            state.panAngle = trajPan;
            state.tiltAngle = trajTilt;
          }

          state.targetBase = (int)constrain(state.panAngle, BASE_MIN, BASE_MAX);
          state.targetNod = (int)constrain(state.tiltAngle, NOD_MIN, NOD_MAX);

          baseOut = state.targetBase;
          nodOut = state.targetNod;
          return true;
        } else {
          state.blindState = GENTLE_SETTLING;
          state.blindFrameCounter = 0;
        }
      }

      if (state.blindState == GENTLE_SETTLING) {
        if (state.blindFrameCounter > SETTLING_FRAMES) {
          state.blindState = NORMAL;
          state.blindFrameCounter = 0;
          isReturningToCenter = false;
        }
      }
    }

    // ═══════════════════════════════════════════════
    // STATE MACHINE (from v5.4)
    // ═══════════════════════════════════════════════

    bool faceDetected = state.active && !state.dataIsStale;

    if (faceDetected) {
      state.framesLost = 0;
      state.framesTracked++;

      if (state.controlState == LOST && state.framesTracked >= FRAMES_TO_ACQUIRE) {
        state.controlState = ACQUIRE;
        trajectory.cancel();
        // State message removed for performance
      }
      else if (state.controlState == ACQUIRE && state.framesTracked >= FRAMES_TO_TRACK) {
        float errorX = abs(state.faceX - CAMERA_CENTER_X);
        float errorY = abs(state.faceY - CAMERA_CENTER_Y);

        if (errorX < ACQUIRE_THRESHOLD && errorY < ACQUIRE_THRESHOLD) {
          state.controlState = TRACK;
          // State message removed for performance
        }
      }
    } else {
      state.framesTracked = 0;
      state.framesLost++;

      if (state.framesLost >= FRAMES_TO_LOST) {
        if (state.controlState != LOST) {
          state.controlState = LOST;
          state.blindState = NORMAL;
        }
      }
    }

    // ═══════════════════════════════════════════════
    // CONTROL
    // ═══════════════════════════════════════════════

    if (state.controlState == ACQUIRE || state.controlState == TRACK) {
      updatePredictiveTracking();
    }
    else if (state.controlState == LOST) {
      updateLost();
    }

    // ═══════════════════════════════════════════════
    // OUTPUT
    // ═══════════════════════════════════════════════

    state.targetBase = (int)constrain(state.panAngle, BASE_MIN, BASE_MAX);
    state.targetNod = (int)constrain(state.tiltAngle, NOD_MIN, NOD_MAX);

    baseOut = state.targetBase;
    nodOut = state.targetNod;

    state.updateCount++;

    return true;
  }


private:

  // ========================================================================
  // PREDICTIVE TRACKING (from Teensy v5.4)
  // ========================================================================

  void updatePredictiveTracking() {
    // Calculate errors
    float errorX = state.faceX - CAMERA_CENTER_X;
    float errorY = state.faceY - CAMERA_CENTER_Y;

    // ═══════════════════════════════════════════════
    // ADAPTIVE DEADBAND (based on confidence)
    // ═══════════════════════════════════════════════

    if (state.controlState == TRACK) {
      // ═══════════════════════════════════════════════════════════════
      // CRITICAL TUNING: Increased deadband to prevent overshoot
      // Old: 6-14 pixels was too sensitive at consistent 50Hz
      // New: 12-20 pixels = more stable, prevents chasing out of frame
      // ═══════════════════════════════════════════════════════════════
      // Higher confidence = tighter deadband
      // Lower confidence = wider deadband (more forgiving)
      float confidenceRatio = state.faceConfidence / 100.0;
      int deadband = 12 + (int)((1.0 - confidenceRatio) * 8);  // 12-20 pixels (was 6-14)

      if (abs(errorX) < deadband) errorX = 0;
      if (abs(errorY) < deadband) errorY = 0;

      // ═══════════════════════════════════════════════════════════════
      // DEBUG: Print error and deadband info
      // ═══════════════════════════════════════════════════════════════
      static unsigned long lastDebug = 0;
      if (millis() - lastDebug > 500) {
        Serial.print("[REFLEX] Face:(");
        Serial.print(state.faceX);
        Serial.print(",");
        Serial.print(state.faceY);
        Serial.print(") Err:(");
        Serial.print((int)errorX);
        Serial.print(",");
        Serial.print((int)errorY);
        Serial.print(") DB:");
        Serial.println(deadband);
        lastDebug = millis();
      }
    }

    float totalError = sqrt(errorX*errorX + errorY*errorY);

    state.errorX = (int)errorX;
    state.errorY = (int)errorY;
    state.errorMagnitude = totalError;

    // ═══════════════════════════════════════════════
    // CONFIDENCE-BASED MOTION SCALING
    // ═══════════════════════════════════════════════

    float motionScale = 1.0;

    // Smooth scaling based on continuous confidence
    // 100 conf → 1.0x speed
    // 75 conf → 0.85x speed
    // 50 conf → 0.65x speed
    // 25 conf → 0.45x speed
    float confidenceScale = 0.4 + (state.faceConfidence / 100.0) * 0.6;
    motionScale *= confidenceScale;

    // Reduce gain during settling period
    if (state.blindState == GENTLE_SETTLING) {
      motionScale *= SETTLING_GAIN_SCALE;
    }

    // Slow down for stationary targets with large error (prevents overshoot)
    float faceSpeed = sqrt(state.faceVX*state.faceVX + state.faceVY*state.faceVY);
    if (faceSpeed < 5.0 && totalError > 40) {
      motionScale *= 0.6;
    }

    // Depth-based scaling (from face size)
    if (state.faceSize > 0) {
      float depthScale = constrain((float)state.faceSize / REFERENCE_FACE_WIDTH, 0.7, 1.2);
      motionScale *= depthScale;
    }

    // ═══════════════════════════════════════════════
    // ADAPTIVE PID
    // ═══════════════════════════════════════════════

    panPID.updateGains(totalError, motionScale);
    tiltPID.updateGains(totalError, motionScale);

    float panCommand = panPID.update(errorX * 0.1, CONTROL_DT);
    float tiltCommand = tiltPID.update(errorY * 0.1, CONTROL_DT);

    // ═══════════════════════════════════════════════
    // VELOCITY LIMITING
    // ═══════════════════════════════════════════════

    panCommand = constrain(panCommand, -MAX_VELOCITY_PER_FRAME, MAX_VELOCITY_PER_FRAME);
    tiltCommand = constrain(tiltCommand, -MAX_VELOCITY_PER_FRAME, MAX_VELOCITY_PER_FRAME);

    // ═══════════════════════════════════════════════
    // APPLICATION WITH SMOOTHING
    // ═══════════════════════════════════════════════

    state.panAngle += panCommand * SMOOTHING_FACTOR;
    state.tiltAngle += tiltCommand * SMOOTHING_FACTOR;

    // ═══════════════════════════════════════════════════════════════
    // DEBUG: Print commands and resulting angles
    // ═══════════════════════════════════════════════════════════════
    static unsigned long lastCmdDebug = 0;
    if (millis() - lastCmdDebug > 500) {
      Serial.print("[REFLEX] Cmd:(");
      Serial.print(panCommand, 2);
      Serial.print(",");
      Serial.print(tiltCommand, 2);
      Serial.print(") Angle:(");
      Serial.print((int)state.panAngle);
      Serial.print(",");
      Serial.print((int)state.tiltAngle);
      Serial.println(")");
      lastCmdDebug = millis();
    }

    // Store adjustments for diagnostics
    state.adjustBase = (int)(panCommand * SMOOTHING_FACTOR);
    state.adjustNod = (int)(tiltCommand * SMOOTHING_FACTOR);
    state.currentGain = panPID.getKp();  // Store current Kp for diagnostics

    // ═══════════════════════════════════════════════
    // OSCILLATION DETECTION
    // ═══════════════════════════════════════════════

    float errorDelta = abs(totalError - state.prevErrorMagnitude);
    if (errorDelta > 10 && totalError < 30) {
      state.oscillationCount++;
    } else if (state.oscillationCount > 0) {
      state.oscillationCount--;
    }
    state.oscillationCount = constrain(state.oscillationCount, 0, 10);

    state.prevErrorMagnitude = totalError;

    // Update tracking quality
    state.trackingQuality = 1.0f - (totalError / 120.0f);
    state.trackingQuality = constrain(state.trackingQuality, 0.0f, 1.0f);

    // Check if settled
    if (totalError < 10) {
      state.isSettled = true;
    } else {
      state.isSettled = false;
    }
  }


  // ========================================================================
  // LOST STATE HANDLING (from Teensy v5.4)
  // ========================================================================

  void updateLost() {
    unsigned long timeLost = millis() - state.lastFaceTime;

    // Short-term prediction (< 1 second)
    if (timeLost < 1000) {
      float predictX = state.faceX + state.faceVX * (timeLost / 1000.0);
      float predictY = state.faceY + state.faceVY * (timeLost / 1000.0);

      float errorX = predictX - CAMERA_CENTER_X;
      float errorY = predictY - CAMERA_CENTER_Y;

      state.panAngle += errorX * 0.01;
      state.tiltAngle += errorY * 0.01;

      state.blindState = NORMAL;
      isReturningToCenter = false;
    }
    // Long-term loss - return to center
    else if (timeLost >= RETURN_TO_CENTER_TIMEOUT_MS) {
      if (!isReturningToCenter) {
        isReturningToCenter = true;
        state.blindState = BLIND_MOVING;
        state.blindFrameCounter = 0;

        trajectory.planReturnToCenter(state.panAngle, state.tiltAngle);
      }

      float trajPan, trajTilt;
      if (trajectory.getNextPosition(trajPan, trajTilt)) {
        state.panAngle = trajPan;
        state.tiltAngle = trajTilt;
      }
    }
    else {
      state.blindState = NORMAL;
      isReturningToCenter = false;
    }
  }


public:

  // ========================================================================
  // FACE REACQUISITION (PRESERVED from original for compatibility)
  // ========================================================================

  void getSearchPosition(int searchStep, int& baseOut, int& nodOut) {
    // Search pattern around last known or center position
    int searchOffsets[8][2] = {
      {0, 0},       // Center
      {-30, 0},     // Left
      {30, 0},      // Right
      {0, -15},     // Up
      {0, 15},      // Down
      {-45, -15},   // Upper left
      {45, -15},    // Upper right
      {0, 0}        // Center again
    };

    int step = searchStep % 8;
    baseOut = constrain((int)state.panAngle + searchOffsets[step][0], BASE_MIN, BASE_MAX);
    nodOut = constrain((int)state.tiltAngle + searchOffsets[step][1], NOD_MIN, NOD_MAX);
  }


  // ========================================================================
  // STATE QUERIES
  // ========================================================================

  bool isActive() const { return state.active; }
  bool isSettled() const { return state.isSettled; }
  float getTrackingQuality() const { return state.trackingQuality; }
  float getErrorMagnitude() const { return state.errorMagnitude; }
  int getUpdateCount() const { return state.updateCount; }

  const ReflexState& getState() const { return state; }


  // ========================================================================
  // DEBUG OUTPUT
  // ========================================================================

  void printDebug() {
    if (state.active) {
      Serial.print("[REFLEX v6.0] ");

      switch(state.controlState) {
        case LOST: Serial.print("LOST"); break;
        case ACQUIRE: Serial.print("ACQ"); break;
        case TRACK: Serial.print("TRK"); break;
      }

      Serial.print(" Face:(");
      Serial.print(state.faceX);
      Serial.print(",");
      Serial.print(state.faceY);
      Serial.print(") Err:");
      Serial.print(state.errorMagnitude, 1);
      Serial.print("px Conf:");
      Serial.print(state.faceConfidence);
      Serial.print(" Pan:");
      Serial.print(state.panAngle, 1);
      Serial.print("° Tilt:");
      Serial.print(state.tiltAngle, 1);
      Serial.print("° Quality:");
      Serial.print(state.trackingQuality * 100, 0);
      Serial.println("%");
    } else {
      Serial.println("[REFLEX v6.0] Inactive");
    }
  }
};

#endif // REFLEXIVE_CONTROL_H
