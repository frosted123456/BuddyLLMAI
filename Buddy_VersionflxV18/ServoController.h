// ServoController.h
// REPLACES: servoParallelControl.h
// Advanced servo control with smooth interpolation, easing curves, and emotion-driven movement

#ifndef SERVO_CONTROLLER_H
#define SERVO_CONTROLLER_H

#include <Servo.h>
#include "MovementStyle.h"

// Forward declarations
extern Servo baseServo;
extern Servo nodServo;
extern Servo tiltServo;

// Track current positions
struct ServoState {
  int basePos;
  int nodPos;
  int tiltPos;
  unsigned long lastUpdate;
};

class ServoController {
private:
  ServoState state;
  
  // Easing curves
  float easeInOut(float t) {
    // Smooth acceleration and deceleration
    return t * t * (3.0f - 2.0f * t);
  }
  
  float easeInOutCubic(float t) {
    // More pronounced ease
    return t < 0.5f ? 4.0f * t * t * t : 1.0f - pow(-2.0f * t + 2.0f, 3.0f) / 2.0f;
  }
  
  float linear(float t) {
    return t;
  }
  
  float applyEasing(float t, float smoothness) {
    if (smoothness > 0.75f) {
      return easeInOutCubic(t);  // Very smooth, confident
    } else if (smoothness > 0.5f) {
      return easeInOut(t);  // Moderately smooth
    } else {
      return linear(t);  // Direct, mechanical
    }
  }
  
  int addJitter(int value, float jitterAmount) {
    if (jitterAmount < 0.01f) return value;
    int maxJitter = (int)(jitterAmount * 8.0f);
    return value + random(-maxJitter, maxJitter + 1);
  }
  
public:
  ServoController() {
    state.basePos = 90;
    state.nodPos = 110;
    state.tiltPos = 85;
    state.lastUpdate = 0;
  }
  
  void initialize(int base, int nod, int tilt) {
    state.basePos = base;
    state.nodPos = nod;
    state.tiltPos = tilt;
    state.lastUpdate = millis();
    
    baseServo.write(base);
    nodServo.write(nod);
    tiltServo.write(tilt);
  }
  
  // ============================================
  // SMOOTH MOVEMENT (uses MovementStyle)
  // ============================================
  
  void smoothMoveTo(int baseTarget, int nodTarget, int tiltTarget, 
                    MovementStyleParams& style) {
    
    int baseStart = state.basePos;
    int nodStart = state.nodPos;
    int tiltStart = state.tiltPos;

    // Calculate distances
    int baseDist = abs(baseTarget - baseStart);
    int nodDist = abs(nodTarget - nodStart);
    int tiltDist = abs(tiltTarget - tiltStart);
    
    // Find longest distance (determines step count)
    int maxDist = max(baseDist, max(nodDist, tiltDist));
    
    if (maxDist < 2) {
      // Already at target
      state.basePos = baseTarget;
      state.nodPos = nodTarget;
      state.tiltPos = tiltTarget;
      return;
    }
    
    // Calculate step count based on speed and distance
    // Fast speed = fewer steps, slow speed = more steps
    int baseSteps = (int)(maxDist * (2.0f - style.speed));
    baseSteps = constrain(baseSteps, 5, 40);  // Min 5, max 40 steps
    
    // Jitter amount from smoothness (inverse)
    float jitterAmount = constrain(1.0f - style.smoothness, 0.0f, 0.5f);

    // Interpolate
    for (int step = 0; step <= baseSteps; step++) {
      float t = (float)step / (float)baseSteps;
      
      // Apply easing curve
      float eased = applyEasing(t, style.smoothness);
      
      // Calculate positions
      int basePos = baseStart + (int)((baseTarget - baseStart) * eased);
      int nodPos = nodStart + (int)((nodTarget - nodStart) * eased);
      int tiltPos = tiltStart + (int)((tiltTarget - tiltStart) * eased);
      
      // Add jitter if low smoothness (anxious/jerky)
      if (jitterAmount > 0.1f && random(100) < 30) {
        basePos = addJitter(basePos, jitterAmount);
        nodPos = addJitter(nodPos, jitterAmount);
        tiltPos = addJitter(tiltPos, jitterAmount);
      }
      
      // Clamp to safe ranges
      basePos = constrain(basePos, 10, 170);
      nodPos = constrain(nodPos, 80, 150);
      tiltPos = constrain(tiltPos, 20, 150);
      
      // Write to servos
      baseServo.write(basePos);
      nodServo.write(nodPos);
      tiltServo.write(tiltPos);
      
      // Update state
      state.basePos = basePos;
      state.nodPos = nodPos;
      state.tiltPos = tiltPos;
      
      // Delay based on speed
      int delayMs = constrain(style.delayMs, 5, 50);
      delay(delayMs);
      
      // Add hesitation pauses
      if (style.hesitation > 0.3f && random(100) < (unsigned int)(style.hesitation * 20.0f)) {
        int pauseMs = (int)(style.hesitation * 150.0f);
        delay(pauseMs);
      }
    }
    
    // Ensure we hit exact target
    baseServo.write(baseTarget);
    nodServo.write(nodTarget);
    tiltServo.write(tiltTarget);
    
    state.basePos = baseTarget;
    state.nodPos = nodTarget;
    state.tiltPos = tiltTarget;
    state.lastUpdate = millis();
  }
  
  // ============================================
  // SINGLE SERVO SMOOTH MOVE
  // ============================================
  
  void smoothMoveServo(Servo& servo, int target, MovementStyleParams& style, 
                       int& statePos) {
    
    int start = statePos;
    int distance = abs(target - start);
    
    if (distance < 2) {
      statePos = target;
      return;
    }
    
    int steps = (int)(distance * (2.0f - style.speed));
    steps = constrain(steps, 3, 30);
    
    for (int step = 0; step <= steps; step++) {
      float t = (float)step / (float)steps;
      float eased = applyEasing(t, style.smoothness);
      
      int pos = start + (int)((target - start) * eased);
      
      // Jitter
      if (style.smoothness < 0.5f && random(100) < 20) {
        pos += random(-3, 4);
      }
      
      pos = constrain(pos, 10, 170);  // Generic safe range
      servo.write(pos);
      statePos = pos;
      
      delay(constrain(style.delayMs, 5, 50));
    }
    
    servo.write(target);
    statePos = target;
  }
  
  // ============================================
  // INSTANT MOVEMENT (for emergency/startup)
  // ============================================
  
  void snapTo(int base, int nod, int tilt) {
    baseServo.write(constrain(base, 10, 170));
    nodServo.write(constrain(nod, 80, 150));
    tiltServo.write(constrain(tilt, 20, 150));
    
    state.basePos = base;
    state.nodPos = nod;
    state.tiltPos = tilt;
    state.lastUpdate = millis();
  }
  
  // ============================================
  // MICRO-MOVEMENTS (subtle life)
  // ============================================
  
  void breathingMotion(float amplitude = 3.0f, int periodMs = 4000) {
    unsigned long now = millis();
    float breathCycle = (now % periodMs) / (float)periodMs;
    
    // Sine wave breathing
    float breathOffset = sin(breathCycle * TWO_PI) * amplitude;
    
    int newNod = state.nodPos + (int)breathOffset;
    newNod = constrain(newNod, 80, 150);
    
    nodServo.write(newNod);
    // Don't update state.nodPos - this is a temporary offset
  }
  
  void weightShift(float maxShift = 5.0f) {
    int shift = random(-(int)maxShift, (int)maxShift + 1);
    int newBase = constrain(state.basePos + shift, 10, 170);
    
    baseServo.write(newBase);
    delay(200);
    baseServo.write(state.basePos);  // Return
  }
  
  void microTilt(float intensity = 1.0f) {
    int shift = random(-4, 5) * intensity;
    int newTilt = constrain(state.tiltPos + shift, 20, 150);

    tiltServo.write(newTilt);
    delay(100);
    tiltServo.write(state.tiltPos);  // Return
  }

  // ============================================
  // REFLEXIVE DIRECT WRITE (bypasses smoothing)
  // ============================================

  /**
   * Direct servo write for reflexive control
   * Bypasses all smoothing, easing, and interpolation
   * Used by reflexive layer for fast face centering
   *
   * This is the "spinal reflex" pathway - below conscious control
   *
   * @param base Target base servo angle (10-170°)
   * @param nod Target nod servo angle (80-150°)
   * @param logOutput If true, print debug info (default: false)
   */
  void directWrite(int base, int nod, bool logOutput = false) {
    // Safety clamping
    base = constrain(base, 10, 170);
    nod = constrain(nod, 80, 150);

    // Write directly to servos (no interpolation)
    baseServo.write(base);
    nodServo.write(nod);
    // Tilt not used for face tracking, keep current position

    // Update state tracking
    state.basePos = base;
    state.nodPos = nod;
    state.lastUpdate = millis();

    // Optional debug output
    if (logOutput) {
      Serial.print("  [REFLEX WRITE] Base:");
      Serial.print(base);
      Serial.print("° Nod:");
      Serial.print(nod);
      Serial.println("°");
    }
  }

  /**
   * Direct servo write with tilt control (three-axis)
   * Similar to directWrite but includes tilt
   *
   * @param base Target base servo angle (10-170°)
   * @param nod Target nod servo angle (80-150°)
   * @param tilt Target tilt servo angle (20-150°)
   * @param logOutput If true, print debug info (default: false)
   */
  void directWriteFull(int base, int nod, int tilt, bool logOutput = false) {
    // Safety clamping
    base = constrain(base, 10, 170);
    nod = constrain(nod, 80, 150);
    tilt = constrain(tilt, 20, 150);

    // Write directly to servos (no interpolation)
    baseServo.write(base);
    nodServo.write(nod);
    tiltServo.write(tilt);

    // Update state tracking
    state.basePos = base;
    state.nodPos = nod;
    state.tiltPos = tilt;
    state.lastUpdate = millis();

    // Optional debug output
    if (logOutput) {
      Serial.print("  [REFLEX WRITE] Base:");
      Serial.print(base);
      Serial.print("° Nod:");
      Serial.print(nod);
      Serial.print("° Tilt:");
      Serial.print(tilt);
      Serial.println("°");
    }
  }

  // ============================================
  // GETTERS & STATE UPDATE
  // ============================================

  int getBasePos() { return state.basePos; }
  int getNodPos() { return state.nodPos; }
  int getTiltPos() { return state.tiltPos; }

  void getPosition(int& base, int& nod, int& tilt) {
    base = state.basePos;
    nod = state.nodPos;
    tilt = state.tiltPos;
  }

  // Update internal state tracking (for external servo writes)
  void updateState(int base, int nod, int tilt) {
    state.basePos = base;
    state.nodPos = nod;
    state.tiltPos = tilt;
    state.lastUpdate = millis();
  }
  
  // ============================================
  // DIAGNOSTICS
  // ============================================
  
  void printState() {
    Serial.println("--- SERVO STATE ---");
    Serial.print("  Base: ");
    Serial.print(state.basePos);
    Serial.print("° Nod: ");
    Serial.print(state.nodPos);
    Serial.print("° Tilt: ");
    Serial.print(state.tiltPos);
    Serial.println("°");
    Serial.print("  Last update: ");
    Serial.print((millis() - state.lastUpdate) / 1000.0f);
    Serial.println(" seconds ago");
  }
};

#endif // SERVO_CONTROLLER_H