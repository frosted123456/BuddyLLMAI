// BehaviorEngine.h
// COMPLETE CONSCIOUSNESS SYSTEM
// Integrates: Expressiveness, Updated IllusionLayer, Episodic Memory, Goal Formation
// V15: Added getSpatialMemory() for Package 3 Vision Integration

#ifndef BEHAVIOR_ENGINE_H
#define BEHAVIOR_ENGINE_H

#include "LittleBots_Board_Pins.h"

// Forward declarations
int checkUltra(int theEchoPin, int theTrigPin);

// ============================================
// PERSON & RELATIONSHIP TRACKING
// ============================================

enum FamiliarityLevel {
  STRANGER = 0,       // 0-2 interactions
  ACQUAINTANCE = 1,   // 3-20 interactions
  FAMILIAR = 2,       // 21-100 interactions
  FAMILY = 3          // 100+ interactions
};

struct PersonRecord {
  int id;
  int interactionCount;
  unsigned long lastSeen;
  unsigned long totalTimeSpent;  // milliseconds
  float averageDistance;
  FamiliarityLevel familiarity;
  bool isValid;

  PersonRecord() {
    id = -1;
    interactionCount = 0;
    lastSeen = 0;
    totalTimeSpent = 0;
    averageDistance = 100.0;
    familiarity = STRANGER;
    isValid = false;
  }
};

#include "Needs.h"
#include "Personality.h"
#include "Emotion.h"
#include "BehaviorSelection.h"
#include "MovementStyle.h"
#include "SpatialMemory.h"
#include "Learning.h"
#include "AttentionSystem.h"
#include "ScanningSystem.h"
#include "IllusionLayer.h"  // UPDATED VERSION
#include "AnimationController.h"
#include "ServoController.h"
#include "BodySchema.h"
#include "MovementExpression.h"
#include "EpisodicMemory.h"  // NEW
#include "GoalFormation.h"   // NEW
#include "OutcomeCalculator.h"  // PACKAGE 4: Standardized outcome measurement
#include "ReflexiveControl.h"   // PHASE 3: Reflexive tracking layer
#include "ConsciousnessLayer.h"
#include "ConsciousnessManifest.h"
#include "AmbientLife.h"
#include "SpeechUrge.h"

// ============================================
// DEBUG CONFIGURATION
// ============================================
#define DEBUG_LEARNING false   // Set false to reduce serial output

class BehaviorEngine {
private:
  Needs needs;
  Personality personality;
  Emotion emotion;
  BehaviorSelection behaviorSelector;
  MovementStyle movementGenerator;
  SpatialMemory spatialMemory;
  Learning learningSystem;
  
  AttentionSystem attention;
  ScanningSystem scanner;
  IllusionLayer illusion;  // UPDATED
  BodySchema bodySchema;
  MovementExpression expressiveness;
  EpisodicMemory episodicMemory;  // NEW
  GoalFormation goalSystem;       // NEW
  OutcomeCalculator outcomeCalc;  // PACKAGE 4: Standardized outcome measurement
  ConsciousnessLayer consciousness;
  ConsciousnessManifest consciousnessManifest;
  AmbientLife ambientLife;
  SpeechUrgeSystem speechUrge;

  AnimationController* animator;
  ServoController* servoController;
  ReflexiveControl* reflexController;  // NEW: Reflexive tracking layer

  unsigned long lastFastUpdate;
  unsigned long lastMediumUpdate;
  unsigned long lastSlowUpdate;
  unsigned long sessionStartTime;
  
  Behavior currentBehavior;
  Behavior previousBehavior;
  int currentDirection;
  float lastDistance;
  float behaviorUncertainty;
  
  int retreatLoopCounter;
  unsigned long lastBehaviorChangeTime;

  // Outcome tracking for learning (storing values, not objects)
  float needsSnapshot_stimulation;
  float needsSnapshot_social;
  float needsSnapshot_energy;
  float needsSnapshot_safety;
  float emotionSnapshot_arousal;
  float emotionSnapshot_valence;
  unsigned long behaviorStartTime;

  // Person tracking
  static const int MAX_PEOPLE = 10;
  PersonRecord people[MAX_PEOPLE];
  int currentPersonID;
  unsigned long personInteractionStart;

  // Face tracking state (ENHANCED with lock-on)
  enum TrackingState {
    TRACK_IDLE,       // Not tracking
    TRACK_ENGAGING,   // Deciding whether to lock on (transition period)
    TRACK_LOCKED,     // Committed tracking with precise centering
    TRACK_DISENGAGING // Losing interest, transitioning out
  };

  bool isTrackingFace;
  TrackingState trackingState;
  unsigned long lastFaceTrackUpdate;
  float targetFaceX;  // Camera coordinates (0-240)
  float targetFaceY;  // Camera coordinates (0-240)
  float trackingIntensity;  // 0.0-1.0 based on familiarity

  // Lock-on mechanism
  unsigned long lockStartTime;
  unsigned long lockDuration;        // How long to maintain lock (varies)
  unsigned long engageStartTime;     // When we started considering engagement
  unsigned long engageDuration;      // Random delay before committing
  int lockedPersonID;                // Person we're locked onto
  bool isRecognizedPerson;           // Whether we recognize them

  // DEBUG MODE: Pure face tracking (no emotions, no behaviors)
  bool debugFaceTrackingMode;

public:
  BehaviorEngine() {
    lastFastUpdate = 0;
    lastMediumUpdate = 0;
    lastSlowUpdate = 0;
    sessionStartTime = millis();
    currentBehavior = IDLE;
    previousBehavior = IDLE;
    currentDirection = 0;
    lastDistance = 100.0;
    behaviorUncertainty = 0.0;

    retreatLoopCounter = 0;
    lastBehaviorChangeTime = millis();

    animator = nullptr;
    servoController = nullptr;
    reflexController = nullptr;

    // Person tracking initialization
    currentPersonID = -1;
    personInteractionStart = 0;

    // Face tracking initialization (ENHANCED with lock-on)
    isTrackingFace = false;
    trackingState = TRACK_IDLE;
    lastFaceTrackUpdate = 0;
    targetFaceX = 120.0;  // Center
    targetFaceY = 120.0;
    trackingIntensity = 0.8;

    // Lock-on initialization
    lockStartTime = 0;
    lockDuration = 0;
    engageStartTime = 0;
    engageDuration = 0;
    lockedPersonID = -1;
    isRecognizedPerson = false;

    // Debug mode initialization
    debugFaceTrackingMode = false;
  }
  
  void setAnimator(AnimationController* anim) {
    animator = anim;
  }

  void setServoController(ServoController* servos) {
    servoController = servos;
  }

  void setReflexController(ReflexiveControl* reflex) {
    reflexController = reflex;
  }

  /**
   * Safely disable reflex controller only if not actively tracking
   * Use this instead of calling disable() directly to prevent
   * interrupting active face tracking operations
   */
  void safeDisableReflex() {
    if (reflexController != nullptr) {
      // Only disable if not currently tracking a face
      if (!isTrackingFace && !reflexController->isActive()) {
        reflexController->disable();
        Serial.println("[BEHAVIOR] Reflex safely disabled");
      } else {
        Serial.println("[BEHAVIOR] Reflex disable blocked - active tracking");
      }
    }
  }

  // GETTERS
  BodySchema& getBodySchema() { return bodySchema; }
  Emotion& getEmotion() { return emotion; }
  Personality& getPersonality() { return personality; }
  AttentionSystem& getAttention() { return attention; }
  Needs& getNeeds() { return needs; }
  SpatialMemory& getSpatialMemory() { return spatialMemory; }  // Package 3: Vision Integration

  // ============================================
  // PERSON TRACKING & RELATIONSHIP
  // ============================================

  PersonRecord* getPerson(int id) {
    for (int i = 0; i < MAX_PEOPLE; i++) {
      if (people[i].isValid && people[i].id == id) {
        return &people[i];
      }
    }
    return nullptr;
  }

  PersonRecord* registerOrUpdatePerson(int id, float distance) {
    PersonRecord* person = getPerson(id);

    if (person == nullptr) {
      // New person - find empty slot
      for (int i = 0; i < MAX_PEOPLE; i++) {
        if (!people[i].isValid) {
          people[i].id = id;
          people[i].interactionCount = 1;
          people[i].lastSeen = millis();
          people[i].totalTimeSpent = 0;
          people[i].averageDistance = distance;
          people[i].familiarity = STRANGER;
          people[i].isValid = true;

          return &people[i];
        }
      }
      return nullptr;  // Database full
    }

    // Update existing person
    person->interactionCount++;
    person->lastSeen = millis();

    // Update average distance (running average)
    person->averageDistance = 0.9 * person->averageDistance + 0.1 * distance;

    // Update familiarity level
    updateFamiliarity(person);

    return person;
  }

  void updateFamiliarity(PersonRecord* person) {
    if (person->interactionCount <= 2) {
      person->familiarity = STRANGER;
    } else if (person->interactionCount <= 20) {
      person->familiarity = ACQUAINTANCE;
    } else if (person->interactionCount <= 100) {
      person->familiarity = FAMILIAR;
    } else {
      person->familiarity = FAMILY;
    }
  }

  float getFamiliarityIntensity(FamiliarityLevel level) {
    switch(level) {
      case STRANGER:      return 0.8;  // High engagement (80%)
      case ACQUAINTANCE:  return 0.5;  // Moderate (50%)
      case FAMILIAR:      return 0.2;  // Low-key (20%)
      case FAMILY:        return 0.1;  // Ambient (10%)
      default:            return 0.5;
    }
  }

  const char* familiarityName(FamiliarityLevel level) {
    switch(level) {
      case STRANGER:      return "Stranger";
      case ACQUAINTANCE:  return "Acquaintance";
      case FAMILIAR:      return "Familiar";
      case FAMILY:        return "Family";
      default:            return "Unknown";
    }
  }

  void handlePersonDetection(int personID, float distance) {
    PersonRecord* person = registerOrUpdatePerson(personID, distance);

    if (person == nullptr) return;  // Database full

    // Start interaction timing
    if (currentPersonID != personID) {
      currentPersonID = personID;
      personInteractionStart = millis();
    }

    // Modulate social need based on familiarity
    float intensity = getFamiliarityIntensity(person->familiarity);
    float socialBoost = intensity * 0.2;  // Scale base boost

    needs.satisfySocial(socialBoost);
  }

  void endPersonInteraction() {
    if (currentPersonID >= 0) {
      PersonRecord* person = getPerson(currentPersonID);
      if (person != nullptr) {
        unsigned long duration = millis() - personInteractionStart;
        person->totalTimeSpent += duration;
      }
      currentPersonID = -1;
    }
  }

  // ============================================
  // ACTIVE FACE TRACKING WITH SERVOS
  // ============================================

  void startFaceTracking(int personID, float faceX, float faceY) {
    // Check if we're already locked onto someone - don't switch mid-lock
    if (trackingState == TRACK_LOCKED) {
      // Just update position if it's the same person
      if (lockedPersonID == personID) {
        updateFaceTracking(faceX, faceY);
      }
      return;  // Ignore other faces while locked
    }

    // CRITICAL FIX: Enable reflex controller when face tracking starts
    if (reflexController != nullptr) {
      reflexController->enable();
    }

    // Get person to determine if recognized
    PersonRecord* person = getPerson(personID);
    isRecognizedPerson = (person != nullptr);

    // Decide whether to engage based on current state
    if (trackingState == TRACK_IDLE || trackingState == TRACK_DISENGAGING) {
      // Start/restart engagement phase (considering whether to pay attention)
      trackingState = TRACK_ENGAGING;
      engageStartTime = millis();

      // Random delay before committing (200ms-800ms) - appears thoughtful
      // Known people: faster engagement (seems like recognition)
      // Unknown people: longer delay (seems like curiosity building)
      if (isRecognizedPerson) {
        engageDuration = random(200, 500);  // Quick recognition
      } else {
        engageDuration = random(400, 800);  // Slower, more curious approach
      }
    } else if (trackingState == TRACK_ENGAGING && lockedPersonID != personID) {
      // Different person during engagement - restart engagement for new person
      engageStartTime = millis();
      isRecognizedPerson = (person != nullptr);
      engageDuration = isRecognizedPerson ? random(200, 500) : random(400, 800);
    }

    targetFaceX = faceX;
    targetFaceY = faceY;
    lockedPersonID = personID;
    isTrackingFace = true;
    lastFaceTrackUpdate = millis();
  }

  void updateFaceTracking(float faceX, float faceY) {
    if (!isTrackingFace) return;

    // Smooth update of target (low-pass filter)
    // When locked, use higher alpha for more responsive tracking
    float alpha = (trackingState == TRACK_LOCKED) ? 0.5 : 0.3;
    targetFaceX = (1.0 - alpha) * targetFaceX + alpha * faceX;
    targetFaceY = (1.0 - alpha) * targetFaceY + alpha * faceY;

    lastFaceTrackUpdate = millis();
  }

  void stopFaceTracking() {
    if (isTrackingFace) {
      Serial.println("[TRACKING] Stopped");
      isTrackingFace = false;
      trackingState = TRACK_IDLE;

      // Disable reflex controller when tracking stops
      if (reflexController != nullptr) {
        reflexController->disable();
      }

      // Reset lock-on state
      lockStartTime = 0;
      lockDuration = 0;
      engageStartTime = 0;
      engageDuration = 0;
      lockedPersonID = -1;
      isRecognizedPerson = false;

      // Optionally return to neutral position
      if (servoController != nullptr) {
        ServoAngles neutral = bodySchema.lookAt(0, 50, 20);
        MovementStyleParams style = movementGenerator.generate(emotion, personality, needs);
        style.speed = 0.3;  // Slow return
        servoController->smoothMoveTo(neutral.base, neutral.nod, neutral.tilt, style);
      }
    }
  }

  void performFaceTracking() {
    unsigned long now = millis();

    if (!isTrackingFace || servoController == nullptr) return;

    // Check if animator is busy (don't interrupt animations)
    if (animator != nullptr && animator->isCurrentlyAnimating()) {
      return;
    }

    // Don't track during certain behaviors
    if (currentBehavior == RETREAT || currentBehavior == REST) {
      return;
    }

    // CRITICAL FIX: If reflex controller is active, let it handle servo movements
    // BehaviorEngine only manages the tracking state machine (ENGAGING/LOCKED/DISENGAGING)
    // but delegates actual servo control to the reflexive layer
    bool reflexIsHandlingMovement = (reflexController != nullptr && reflexController->isActive());

    // ==========================================
    // STATE MACHINE: Handle attention states
    // ==========================================

    switch (trackingState) {
      case TRACK_IDLE:
        // Not tracking - nothing to do
        return;

      case TRACK_ENGAGING:
        // Considering whether to lock on - have we waited long enough?
        if (now - engageStartTime >= engageDuration) {
          // Decision made - commit to lock!
          trackingState = TRACK_LOCKED;
          lockStartTime = now;

          // Determine lock duration based on recognition
          if (isRecognizedPerson) {
            // Known person: 5-12 seconds (acknowledging/checking in)
            lockDuration = random(5000, 12000);
          } else {
            // Unknown person: 8-15 seconds (studying/learning)
            lockDuration = random(8000, 15000);
          }

          Serial.print("[TRACKING] LOCKED ON for ");
          Serial.print(lockDuration / 1000);
          Serial.println(" seconds");
        }
        // During engagement, track moderately (building interest but visible)
        trackingIntensity = 0.7;  // Increased from 0.4 for more visible tracking
        break;

      case TRACK_LOCKED:
        // Fully committed tracking - has lock expired?
        if (now - lockStartTime >= lockDuration) {
          // Lock period over - start disengaging
          trackingState = TRACK_DISENGAGING;
        }
        // During lock: PRECISE tracking regardless of familiarity
        trackingIntensity = 0.95;  // Very high intensity for centering
        break;

      case TRACK_DISENGAGING:
        // Gradually losing interest - could return to neutral or wait for new face
        trackingIntensity = 0.2;  // Low intensity
        // Note: Will transition to IDLE when stopFaceTracking() is called
        break;
    }

    // ==========================================
    // Update rate based on state
    // ==========================================

    unsigned long updateInterval;
    switch (trackingState) {
      case TRACK_ENGAGING:
        updateInterval = 100;   // Moderate updates during consideration
        break;
      case TRACK_LOCKED:
        updateInterval = 200;   // 200ms = 5 Hz updates (gives servo time to physically move)
        break;
      case TRACK_DISENGAGING:
        updateInterval = 150;  // Slow updates while losing interest
        break;
      default:
        updateInterval = 100;
    }

    static unsigned long lastUpdate = 0;
    if (now - lastUpdate < updateInterval) {
      return;  // Not time to update yet
    }
    lastUpdate = now;

    // ==========================================
    // SERVO-RELATIVE ADJUSTMENT (FIXED)
    // Convert camera-relative error to servo adjustments
    // ==========================================

    // Camera frame: 0-240, center at 120
    float centerX = 120.0;
    float centerY = 120.0;

    // Calculate error from center (camera-relative)
    float errorX = targetFaceX - centerX;  // Pixels from center (-120 to +120)
    float errorY = targetFaceY - centerY;  // Pixels from center (-120 to +120)

    // Apply deadband to prevent jitter when centered
    const float DEADBAND = 5.0;  // Pixels
    if (abs(errorX) < DEADBAND) errorX = 0;
    if (abs(errorY) < DEADBAND) errorY = 0;

    // Get current servo positions (CRITICAL: Use current position as reference)
    int currentBase = servoController->getBasePos();
    int currentNod = servoController->getNodPos();
    int currentTilt = servoController->getTiltPos();

    // Calculate servo adjustments from camera error (proportional control)
    // Gain varies by tracking state for different responsiveness
    float baseGain, nodGain, tiltGain;
    switch (trackingState) {
      case TRACK_ENGAGING:
        baseGain = 0.20;   // Moderate response during engagement
        nodGain = 0.15;
        tiltGain = 0.10;
        break;
      case TRACK_LOCKED:
        baseGain = 0.30;   // Higher gain for precise centering
        nodGain = 0.25;
        tiltGain = 0.15;
        break;
      case TRACK_DISENGAGING:
        baseGain = 0.10;   // Low response while losing interest
        nodGain = 0.08;
        tiltGain = 0.05;
        break;
      default:
        baseGain = 0.15;
        nodGain = 0.12;
        tiltGain = 0.08;
    }

    // Convert pixel error to servo adjustment (SERVO-RELATIVE, not absolute!)
    // CRITICAL: Camera rotation causes coordinate inversion!
    //
    // When face is LEFT in camera (errorX < 0):
    //   Camera is pointing too far left → need to turn RIGHT (increase base angle)
    // When face is RIGHT in camera (errorX > 0):
    //   Camera is pointing too far right → need to turn LEFT (decrease base angle)
    // THEREFORE: baseAdjustment has OPPOSITE sign from errorX!
    float baseAdjustment = -errorX * baseGain;  // INVERTED: Face left → turn right, Face right → turn left
    float nodAdjustment = errorY * nodGain * 0.3;  // Nod has smaller effect
    float tiltAdjustment = errorX * tiltGain * 0.2;  // Tilt compensates (also inverted)

    // Apply adjustments to current positions (KEY FIX: relative to current, not absolute!)
    int targetBase = currentBase + (int)baseAdjustment;
    int targetNod = currentNod + (int)nodAdjustment;
    int targetTilt = currentTilt + (int)tiltAdjustment;

    // Generate smooth movement with personality
    MovementStyleParams style = movementGenerator.generate(emotion, personality, needs);

    // Speed varies by state
    switch (trackingState) {
      case TRACK_ENGAGING:
        style.speed = 0.7;  // Moderate speed during engagement
        style.smoothness = 0.5;  // Fairly smooth
        break;
      case TRACK_LOCKED:
        style.speed = 1.5;  // VERY fast for responsive tracking
        style.smoothness = 0.2;  // Low smoothness for immediate response
        break;
      case TRACK_DISENGAGING:
        style.speed = 0.3;  // Slow, losing interest
        style.smoothness = 0.6;  // Smooth disengagement
        break;
      default:
        style.speed = 0.5;
        style.smoothness = 0.5;
    }

    // Clamp to safe ranges
    targetBase = constrain(targetBase, 10, 170);
    targetNod = constrain(targetNod, 80, 150);
    targetTilt = constrain(targetTilt, 20, 150);

    // Calculate movement magnitude
    int baseDelta = abs(targetBase - currentBase);
    int nodDelta = abs(targetNod - currentNod);
    int tiltDelta = abs(targetTilt - currentTilt);

    // CRITICAL FIX: If reflex controller is active, skip servo commands
    // Let the reflex layer handle precise servo control with its PID system
    if (reflexIsHandlingMovement) {
      // Reflex controller is doing the servo movements - we only managed state machine
      return;
    }

    // Only send command if movement is significant (prevents servo spam)
    // Servos need ~100-200ms to move, so rapid tiny updates cause drift
    const int MIN_MOVEMENT = 2;  // degrees

    if (baseDelta >= MIN_MOVEMENT || nodDelta >= MIN_MOVEMENT || tiltDelta >= MIN_MOVEMENT) {
      // During LOCKED state, use smoothMoveTo with fast speed for responsive tracking
      // (Direct writes cause servo spam and accumulation drift)
      if (trackingState == TRACK_LOCKED) {
        // Use smooth movement but with very fast speed
        MovementStyleParams fastStyle = style;
        fastStyle.speed = 1.8;  // Very fast
        fastStyle.smoothness = 0.1;  // Minimal smoothing
        fastStyle.delayMs = 5;  // Minimal delay between steps
        servoController->smoothMoveTo(targetBase, targetNod, targetTilt, fastStyle);
      } else {
        servoController->smoothMoveTo(targetBase, targetNod, targetTilt, style);
      }
    }
    // else: Skip update - change too small to warrant servo movement
  }

  // Timeout check (call in main update loop)
  void checkFaceTrackingTimeout() {
    if (isTrackingFace) {
      unsigned long now = millis();

      // Different timeout behavior based on state
      switch (trackingState) {
        case TRACK_ENGAGING:
          // During engagement, lose interest quickly if face disappears
          if (now - lastFaceTrackUpdate > 800) {
            stopFaceTracking();
          }
          break;

        case TRACK_LOCKED:
          // During lock, be more tolerant (face might move in/out of frame)
          if (now - lastFaceTrackUpdate > 2000) {  // 2 second tolerance
            trackingState = TRACK_DISENGAGING;
            // Don't stop immediately - let it disengage naturally
          }
          break;

        case TRACK_DISENGAGING:
          // During disengagement, stop after short timeout
          if (now - lastFaceTrackUpdate > 1500) {
            stopFaceTracking();
          }
          break;

        case TRACK_IDLE:
        default:
          // Nothing to do
          break;
      }
    }
  }

  // Getter for tracking state
  bool getIsTrackingFace() { return isTrackingFace; }

  // ============================================
  // DEBUG MODE: Pure Face Tracking
  // ============================================

  void toggleDebugFaceTracking() {
    debugFaceTrackingMode = !debugFaceTrackingMode;

    if (debugFaceTrackingMode) {
      Serial.println("\n[DEBUG] Face tracking mode ENABLED");
      Serial.println("  Type 'x' again to exit\n");

      // Force tracking to be ready
      isTrackingFace = false;
      trackingState = TRACK_IDLE;

      // Move to neutral position
      if (servoController != nullptr) {
        servoController->snapTo(90, 110, 85);
      }
    } else {
      Serial.println("\n[DEBUG] Face tracking mode DISABLED\n");
      // Stop tracking
      stopFaceTracking();
    }
  }

  void debugUpdate() {
    // Simplified update loop for debug mode - ONLY face tracking
    if (!debugFaceTrackingMode) return;

    unsigned long now = millis();

    // In debug mode, force LOCKED state immediately when tracking
    if (isTrackingFace && trackingState != TRACK_LOCKED) {
      trackingState = TRACK_LOCKED;
      lockStartTime = now;
      lockDuration = 3600000;  // 1 hour - effectively infinite
      trackingIntensity = 0.95;  // Maximum precision
    }

    // Only do face tracking - nothing else
    if (servoController != nullptr) {
      performFaceTracking();
    }
  }

  bool isDebugMode() { return debugFaceTrackingMode; }

  // ============================================
  // LEARNING OUTCOME CALCULATION
  // ============================================
  
  void snapshotStateBeforeBehavior() {
    // PACKAGE 4: Use standardized outcome calculator
    outcomeCalc.snapshotState(needs, emotion);
    behaviorStartTime = millis();
  }
  
  float calculateBehaviorOutcome() {
    // PACKAGE 4: Use standardized outcome calculator
    float outcome = outcomeCalc.calculate(
      currentBehavior,
      needs,
      emotion,
      &goalSystem  // Pass goal system
    );

    #if DEBUG_LEARNING
    Serial.print("[OUTCOME] ");
    Serial.print(behaviorToString(currentBehavior));
    Serial.print(": ");
    Serial.println(outcome, 3);

    // Optional: Show detailed breakdown
    // outcomeCalc.printBreakdown(currentBehavior, needs, emotion, &goalSystem);
    #endif

    return outcome;
  }
  
  void begin() {
    Serial.println("\n[SYSTEM] Initializing behavior engine...");

    learningSystem.loadFromEEPROM(personality, behaviorSelector);
    snapshotStateBeforeBehavior();

    Serial.println("[SYSTEM] Behavior engine ready\n");
  }
  
  void update(float sensorDistance, int baseAngle, int nodAngle) {
    // DEBUG MODE: Skip all normal processing, only do face tracking
    if (debugFaceTrackingMode) {
      debugUpdate();
      return;
    }

    unsigned long now = millis();
    float deltaTime = (now - lastFastUpdate) / 1000.0;

    bodySchema.updateCurrentAngles(baseAngle, nodAngle, 85);

    // ═══════════════════════════════════════════════════════════════════
    // CRITICAL FIX: Check reflex status once at top
    // ═══════════════════════════════════════════════════════════════════
    bool reflexIsActive = (reflexController != nullptr && reflexController->isActive());

    // ═══════════════════════════════════════════════════════════════════
    // VERIFICATION: Log which path is taken (every 5 seconds)
    // ═══════════════════════════════════════════════════════════════════
    static unsigned long lastPathLog = 0;
    if (now - lastPathLog > 5000) {
      if (reflexIsActive) {
        Serial.println("[BEHAVIOR] FAST PATH: Reflex active, minimal processing");
      } else {
        Serial.println("[BEHAVIOR] NORMAL PATH: Full behavior system active");
      }
      lastPathLog = now;
    }

    // ═══════════════════════════════════════════════════════════════════
    // OPTIMIZATION: Minimal processing when reflex is active
    // ═══════════════════════════════════════════════════════════════════
    if (reflexIsActive) {
      // Fast path: Only essential updates during tracking

      // Update fast loop (for emotion/needs decay)
      if (now - lastFastUpdate > 0) {
        fastUpdate(sensorDistance, baseAngle, nodAngle, deltaTime);
        lastFastUpdate = now;
      }

      // Check face tracking timeout
      checkFaceTrackingTimeout();

      // Skip everything else - return early!
      return;
    }

    // ═══════════════════════════════════════════════════════════════════
    // NORMAL PATH: Full behavior processing when NOT tracking
    // ═══════════════════════════════════════════════════════════════════

    // Guard ambient monitoring
    if (attention.needsAmbientUpdate()) {
      scanner.ambientMonitoring(spatialMemory);
      attention.markAmbientUpdate();
    }

    if (now - lastFastUpdate > 0) {
      fastUpdate(sensorDistance, baseAngle, nodAngle, deltaTime);
      lastFastUpdate = now;
    }

    attention.update(spatialMemory, personality, deltaTime);

    // No guards needed - reflex already returned early if active
    if (attention.needsPeripheralSweep()) {
      executePeripheralSweep();
      attention.markPeripheralSweep();
    }

    if (attention.needsFovealScan()) {
      executeFovealScan();
      attention.markFovealScan();
    }

    if (now - lastMediumUpdate > 5000) {
      mediumUpdate(deltaTime);

      // Execute behavior on change
      if (currentBehavior != previousBehavior && animator != nullptr) {
        // ═══════════════════════════════════════════════════════════════
        // VERIFICATION: Log that we're executing normal behavior
        // ═══════════════════════════════════════════════════════════════
        Serial.print("[BEHAVIOR] Executing normal behavior: ");
        Serial.println(behaviorToString(currentBehavior));

        executeCurrentBehavior();
      }

      lastMediumUpdate = now;
    }

    if (now - lastSlowUpdate > 30000) {
      slowUpdate();
      lastSlowUpdate = now;
    }

    // Micro-movements and expressions (no guard needed, reflex returned early)
    if (animator != nullptr && !animator->isCurrentlyAnimating()) {
      animator->updateMicroMovements(currentBehavior, emotion);

      if (servoController != nullptr && currentBehavior != RETREAT) {
        expressiveness.performQuirk(*servoController, personality, needs);
      }

      performFaceTracking();  // Note: This already has internal reflex check

      // === CONSCIOUSNESS MANIFESTATION ===
      if (servoController != nullptr) {
        // Wondering state (very rare, existential)
        if (consciousness.isWondering()) {
          consciousnessManifest.manifestWondering(
              consciousness.getWonderingType(),
              consciousness.getWonderingIntensity(),
              *servoController, emotion, personality, needs);
        }
        // Conflict visible as false starts
        else if (consciousness.shouldShowFalseStart()) {
          consciousnessManifest.manifestConflict(
              consciousness.getConflict(),
              *servoController, bodySchema, emotion, personality, needs);
        }
        // Counterfactual thinking (subtle replays)
        else if (consciousness.isCounterfactualThinking() && currentBehavior == IDLE) {
          consciousnessManifest.manifestCounterfactual(
              consciousness.getCounterfactual(),
              *servoController, currentDirection);
        }
        // Meta-awareness catch
        if (consciousness.didCatchMyself()) {
          consciousnessManifest.manifestMetaCatch(
              *servoController, emotion, personality, needs);
        }
      }
    }

    // Ambient life (need-driven, not timer-driven)
    if (!reflexIsActive && !isTrackingFace &&
        (animator == nullptr || !animator->isCurrentlyAnimating()) &&
        servoController != nullptr) {
      ambientLife.update(needs, emotion, personality, *servoController, now);
    }

    checkFaceTrackingTimeout();
    checkStuckState();
  }
  
  void fastUpdate(float distance, int baseAngle, int nodAngle, float dt) {
    currentDirection = scanner.angleToDirection(baseAngle, nodAngle);

    float distanceChange = abs(distance - lastDistance);
    float novelty = spatialMemory.getNovelty(currentDirection);

    emotion.update(needs, personality, distance, distanceChange, novelty, dt);
    respondToNovelty(novelty, currentDirection);

    lastDistance = distance;
  }
  
  void mediumUpdate(float dt) {
    needs.update(dt, personality, spatialMemory);

    BehaviorScore scores[8];
    int numBehaviors = behaviorSelector.scoreAllBehaviors(needs, personality, emotion,
                                                           spatialMemory, currentDirection, scores);

    // Update consciousness with behavior scores
    consciousness.update(scores, numBehaviors, needs, emotion, personality,
                          spatialMemory, dt);

    // Update speech urge from internal states
    speechUrge.update(
        needs, emotion, personality,
        consciousness.isWondering(),
        consciousness.isInConflict(),
        consciousness.getTension(),
        isTrackingFace,
        isRecognizedPerson,
        spatialMemory.getTotalNovelty(),
        millis()
    );

    Behavior selected = behaviorSelector.selectBehavior(scores, numBehaviors);

    // GOAL SYSTEM INFLUENCES BEHAVIOR
    if (goalSystem.hasActiveGoal()) {
      Behavior goalSuggestion = goalSystem.pursueSuggestedBehavior(selected, personality);

      if (goalSuggestion != selected) {
        selected = goalSuggestion;
      }
    }

    if (behaviorSelector.isStuck()) {
      selected = behaviorSelector.forceAlternativeBehavior(scores, numBehaviors);
      needs.forceExplorationDrive();
    }
    
    // BEFORE behavior change: record outcome of previous behavior
    if (currentBehavior != IDLE && previousBehavior != IDLE) {
      // Calculate how well the previous behavior worked
      float outcome = calculateBehaviorOutcome();
      
      // Record to learning system
      learningSystem.recordOutcome(previousBehavior, outcome);
    }

    previousBehavior = currentBehavior;
    currentBehavior = selected;

    // Record execution for variety tracking
    behaviorSelector.recordBehaviorExecution(selected);

    // AFTER behavior change: snapshot state for next outcome calculation
    if (currentBehavior != previousBehavior) {
      lastBehaviorChangeTime = millis();
      snapshotStateBeforeBehavior();
    }

    // Calculate uncertainty
    float topScore = scores[0].finalScore;
    float secondScore = scores[1].finalScore;
    for (int i = 2; i < numBehaviors; i++) {
      if (scores[i].finalScore > topScore) {
        secondScore = topScore;
        topScore = scores[i].finalScore;
      } else if (scores[i].finalScore > secondScore) {
        secondScore = scores[i].finalScore;
      }
    }

    behaviorUncertainty = 1.0 - (topScore - secondScore);
    behaviorUncertainty = constrain(behaviorUncertainty, 0.0, 1.0);

    // CONSIDER FORMING GOALS
    if (goalSystem.shouldFormGoal(currentBehavior, emotion, personality,
                                   spatialMemory.getTotalNovelty(), needs.getSocial())) {
      formAppropriateGoal();
    }

    // Chance of counterfactual thinking after behavior outcome
    if (behaviorUncertainty > 0.4 && random(100) < 15) {
      float outcome = calculateBehaviorOutcome();
      consciousness.triggerCounterfactual(currentBehavior,
                                           consciousness.getSuppressedDrive(),
                                           outcome);
    }

    // Record significant actions in narrative
    if (currentBehavior != IDLE && currentBehavior != REST &&
        currentBehavior != previousBehavior) {
      float outcome = calculateBehaviorOutcome();
      consciousness.recordSignificantAction(currentBehavior, outcome);
    }
  }
  
  void slowUpdate() {
    float sessionQuality = calculateSessionQuality();
    learningSystem.consolidate(sessionQuality);

    personality.drift(learningSystem, 0.001);
    episodicMemory.consolidate();
  }
  
  void checkStuckState() {
    if (currentBehavior == RETREAT) {
      retreatLoopCounter++;

      if (retreatLoopCounter > 5) {
        needs.successfulRetreat();
        retreatLoopCounter = 0;
      }
    } else {
      if (retreatLoopCounter > 0) {
        retreatLoopCounter--;
      }
    }
  }

  // ============================================
  // PACKAGE 4: NOVELTY RESPONSE SYSTEM
  // ============================================

  void respondToNovelty(float novelty, int direction) {
    // Only respond to significant novelty
    if (novelty < 0.6) return;

    // Check if we're in a behavior that should care about novelty
    if (currentBehavior == RETREAT || currentBehavior == REST) {
      return;  // Don't interrupt these
    }

    // Satisfy needs based on novelty level
    if (novelty > 0.8) {
      needs.satisfyStimulation(0.15);
      needs.satisfyNovelty(0.20);
    } else if (novelty > 0.6) {
      needs.satisfyStimulation(0.08);
      needs.satisfyNovelty(0.10);
    }

    // Focus attention on novel direction
    attention.setFocusDirection(direction);
  }

  // ============================================
  // NEW: GOAL FORMATION
  // ============================================
  
  void formAppropriateGoal() {
    int focusDir = attention.getFocusDirection();
    float focusDist = spatialMemory.getAverageDistance(focusDir);
    
    GoalType goalType = GOAL_NONE;
    
    // Choose goal based on context
    if (spatialMemory.getTotalNovelty() > 0.7) {
      goalType = GOAL_INVESTIGATE_THOROUGHLY;
    } else if (needs.getSocial() > 0.7) {
      goalType = GOAL_SEEK_SOCIAL;
    } else if (attention.getMaxSalience() > 0.6) {
      goalType = GOAL_UNDERSTAND_PATTERN;
    } else if (personality.getPlayfulness() > 0.6 && needs.getEnergy() > 0.5) {
      goalType = GOAL_EXPERIMENT;
    } else if (needs.getEnergy() < 0.3) {
      goalType = GOAL_REST_FULLY;
    } else {
      goalType = GOAL_EXPLORE_AREA;
    }
    
    goalSystem.formGoal(goalType, focusDir, focusDist, personality, emotion);
  }
  
  // ============================================
  // SPATIAL SCANNING
  // ============================================
  
  void executePeripheralSweep() {
    if (servoController == nullptr) return;

    MovementStyleParams style = movementGenerator.generate(emotion, personality, needs);

    // ═══════════════════════════════════════════════════════════════
    // PERFORMANCE: Non-blocking scan - track position and advance
    // ═══════════════════════════════════════════════════════════════
    static int scanIndex = 0;
    SpatialPoint points[8];
    int count = 0;
    bodySchema.generateScanPattern(points, count, 8, 30.0, 80.0);

    if (scanIndex >= count || count == 0) {
      scanIndex = 0;  // Reset to beginning
    }

    // Command one scan point per call instead of all at once
    ServoAngles angles = bodySchema.lookAt(points[scanIndex].x, points[scanIndex].y, points[scanIndex].z);
    servoController->smoothMoveTo(angles.base, angles.nod, angles.tilt, style);
    // REMOVED: delay(150) - non-blocking design

    float distance = checkUltra(echoPin, trigPin);
    int direction = angles.base / 22;
    spatialMemory.updateReading(direction, distance);

    scanIndex++;  // Advance to next scan point for next call
  }

  void executeFovealScan() {
    if (servoController == nullptr) return;

    int focusDir = attention.getFocusDirection();
    float distance = spatialMemory.getAverageDistance(focusDir);

    bodySchema.setAttentionDirection(focusDir, distance, 0.9);

    MovementStyleParams style = movementGenerator.generate(emotion, personality, needs);

    // ═══════════════════════════════════════════════════════════════
    // PERFORMANCE: Non-blocking foveal scan - track position
    // ═══════════════════════════════════════════════════════════════
    static int fovealStep = 0;

    if (fovealStep == 0) {
      // First step: center on target
      ServoAngles center = bodySchema.lookAtDirection(focusDir, distance);
      servoController->smoothMoveTo(center.base, center.nod, center.tilt, style);
      // REMOVED: delay(300) - non-blocking design
    } else {
      // Subsequent steps: track attention
      ServoAngles track = bodySchema.trackAttention(0.3);
      servoController->smoothMoveTo(track.base, track.nod, track.tilt, style);
      // REMOVED: delay(250) in loop - non-blocking design

      float dist = checkUltra(echoPin, trigPin);
      spatialMemory.updateReading(focusDir, dist);
    }

    fovealStep++;
    if (fovealStep >= 4) {
      fovealStep = 0;  // Reset for next scan
      bodySchema.clearAttention();
    }
  }
  
  // ============================================
  // BEHAVIOR EXECUTION (WITH FULL CONSCIOUSNESS)
  // ============================================
  
  void executeCurrentBehavior() {
    // ═══════════════════════════════════════════════════════════════════
    // CRITICAL FIX: Don't disable reflex if actively tracking a face
    // ═══════════════════════════════════════════════════════════════════
    if (reflexController != nullptr) {
      // Debug: Log behavior execution and reflex state
      Serial.print("[BEHAVIOR] Executing: ");
      Serial.print(behaviorToString(currentBehavior));
      Serial.print(" | Reflex: ");
      Serial.print(reflexController->isActive() ? "ACTIVE" : "inactive");
      Serial.print(" | Face tracking: ");
      Serial.print(isTrackingFace ? "YES" : "no");

      // Only manage reflex if NOT currently tracking
      if (!isTrackingFace && !reflexController->isActive()) {
        // Safe to disable for non-social behaviors
        if (currentBehavior != SOCIAL_ENGAGE && currentBehavior != INVESTIGATE) {
          Serial.println(" → Disabling reflex");
          reflexController->disable();
        } else {
          Serial.println(" → Enabling reflex (social behavior)");
        }
      } else {
        // Reflex is actively tracking - leave it alone!
        Serial.println(" → Reflex protected (tracking active)");
      }
    }

    // Recall similar past experiences
    Episode recalled;
    if (episodicMemory.recallSimilar(currentBehavior, currentDirection,
                                     lastDistance, recalled) >= 0) {
      // Influence current behavior based on past
      if (recalled.outcome < 0.3 && recalled.wasSuccessful == false) {
        // Could modify behavior here
      }
    }

    switch(currentBehavior) {
      case IDLE:
        executeIdle();
        break;
      case EXPLORE:
        executeExplore();
        break;
      case INVESTIGATE:
        executeInvestigate();
        break;
      case SOCIAL_ENGAGE:
        executeSocialEngage();
        break;
      case RETREAT:
        executeRetreat();
        break;
      case REST:
        executeRest();
        break;
      case PLAY:
        executePlay();
        break;
      case VIGILANT:
        executeVigilant();
        break;
    }
    
    applyIllusion();  // UPDATED VERSION
    
    float outcome = calculateBehaviorOutcome();
    recordBehaviorOutcome(currentBehavior, outcome);
    
    // NEW: Record experience in episodic memory
    episodicMemory.recordEpisode(currentBehavior, emotion.getLabel(),
                                 lastDistance, currentDirection,
                                 spatialMemory.likelyHumanPresent(), outcome);
    
    // NEW: Update goal progress
    if (goalSystem.hasActiveGoal()) {
      goalSystem.recordProgress(currentBehavior, outcome);
    }
  }
  
  void executeIdle() {
    // ═══════════════════════════════════════════════════════════
    // VERIFICATION: Confirm behavior is executing
    // ═══════════════════════════════════════════════════════════
    static unsigned long lastIdleLog = 0;
    if (millis() - lastIdleLog > 3000) {  // Log every 3 seconds max
      Serial.println("[BEHAVIOR] IDLE: Resting in neutral position");
      lastIdleLog = millis();
    }

    ServoAngles neutral = bodySchema.lookAt(0, 50, 20);

    if (animator != nullptr && servoController != nullptr) {
      MovementStyleParams style = movementGenerator.generate(emotion, personality, needs);
      servoController->smoothMoveTo(neutral.base, neutral.nod, neutral.tilt, style);
    }

    needs.consumeEnergy(-0.02);
  }

  void executeExplore() {

    ServoAngles target = bodySchema.exploreRandomly(30.0, 80.0);

    if (animator != nullptr && servoController != nullptr) {
      // ═══════════════════════════════════════════════════════════════
      // VERIFICATION: Log that servo command is being sent
      // ═══════════════════════════════════════════════════════════════
      static unsigned long lastExploreLog = 0;
      if (millis() - lastExploreLog > 5000) {
        Serial.print("[EXPLORE] Commanding servos: Base=");
        Serial.print(target.base);
        Serial.print("° Nod=");
        Serial.print(target.nod);
        Serial.print("° Tilt=");
        Serial.println(target.tilt);
        lastExploreLog = millis();
      }

      MovementStyleParams style = movementGenerator.generate(emotion, personality, needs);
      servoController->smoothMoveTo(target.base, target.nod, target.tilt, style);
      // ═══════════════════════════════════════════════════════════════
      // PERFORMANCE: Removed delay(300) - was blocking processor!
      // Behavior called repeatedly by main loop, no need to wait
      // ═══════════════════════════════════════════════════════════════

      if (expressiveness.canExpress() && random(100) < 35) {
        expressiveness.expressCuriosity(*servoController, emotion, personality, needs);
        // REMOVED: delay(200) - non-blocking design
      }

      // ═══════════════════════════════════════════════════════════════
      // PERFORMANCE: Simplified - removed loop with delays
      // Command one additional nearby exploration point per call
      // ═══════════════════════════════════════════════════════════════
      ServoAngles nearby = bodySchema.exploreRandomly(25.0, 70.0);
      servoController->smoothMoveTo(nearby.base, nearby.nod, nearby.tilt, style);
      // REMOVED: delay(200) in loop - non-blocking design
    }
    
    needs.satisfyStimulation(0.15);
    needs.consumeEnergy(0.05);
  }
  
  void executeInvestigate() {
    // ═══════════════════════════════════════════════════════════
    // VERIFICATION: Confirm behavior is executing
    // ═══════════════════════════════════════════════════════════
    static unsigned long lastInvestigateLog = 0;
    if (millis() - lastInvestigateLog > 3000) {  // Log every 3 seconds max
      Serial.println("[BEHAVIOR] INVESTIGATE: Examining point of interest");
      lastInvestigateLog = millis();
    }

    // Enable reflexive face tracking (if investigating a face)
    if (reflexController != nullptr) {
      reflexController->enable();
    }

    int focusDir = attention.getFocusDirection();
    float distance = spatialMemory.getAverageDistance(focusDir);

    ServoAngles angles = bodySchema.lookAtDirection(focusDir, distance);
    bodySchema.setAttentionDirection(focusDir, distance, 0.8);

    // ═══════════════════════════════════════════════════════════════════
    // CRITICAL FIX: Check if reflex is handling movement
    // ═══════════════════════════════════════════════════════════════════
    bool reflexIsHandlingMovement = (reflexController != nullptr &&
                                     reflexController->isActive());

    if (!reflexIsHandlingMovement && animator != nullptr && servoController != nullptr) {
      // Only move servos if reflex is NOT active
      MovementStyleParams style = movementGenerator.generate(emotion, personality, needs);

      servoController->smoothMoveTo(angles.base, angles.nod, angles.tilt, style);
      // REMOVED: delay(400) - non-blocking design

      if (expressiveness.canExpress() && random(100) < 50) {
        if (personality.getCuriosity() > 0.5) {
          expressiveness.expressCuriosity(*servoController, emotion, personality, needs);
        } else {
          expressiveness.expressContemplation(*servoController, emotion, personality, needs);
        }
        // REMOVED: delay(200) - non-blocking design
      }

      // ═══════════════════════════════════════════════════════════════
      // PERFORMANCE: Simplified tracking - removed loop with delays
      // Command one tracking update per call instead of 3 with delays
      // ═══════════════════════════════════════════════════════════════
      ServoAngles track = bodySchema.trackAttention(0.3);
      servoController->smoothMoveTo(track.base, track.nod, track.tilt, style);
      // REMOVED: delay(300) in loop - non-blocking design

      if (random(100) < 25) {
        expressiveness.applyNaturalCorrection(*servoController);
      }
    }
    // else: Reflex is active - let it handle ALL servo movements

    bodySchema.clearAttention();
    needs.satisfyNovelty(0.2);
    needs.satisfyStimulation(0.1);
    needs.consumeEnergy(0.03);
  }
  
  void executeSocialEngage() {
    // ═══════════════════════════════════════════════════════════
    // VERIFICATION: Confirm behavior is executing
    // ═══════════════════════════════════════════════════════════
    static unsigned long lastSocialLog = 0;
    if (millis() - lastSocialLog > 3000) {  // Log every 3 seconds max
      Serial.println("[BEHAVIOR] SOCIAL_ENGAGE: Interacting with human");
      lastSocialLog = millis();
    }

    // Enable reflexive face tracking
    if (reflexController != nullptr) {
      reflexController->enable();
    }

    float distance = 60.0;
    ServoAngles angles = bodySchema.lookAt(0, distance, 25);
    bodySchema.setAttentionTarget(SpatialPoint(0, distance, 25), 1.0);

    // ═══════════════════════════════════════════════════════════════════
    // CRITICAL FIX: Check if reflex is handling movement
    // ═══════════════════════════════════════════════════════════════════
    bool reflexIsHandlingMovement = (reflexController != nullptr &&
                                     reflexController->isActive());

    if (!reflexIsHandlingMovement && animator != nullptr && servoController != nullptr) {
      // Only move servos if reflex is NOT active
      MovementStyleParams style = movementGenerator.generate(emotion, personality, needs);

      servoController->smoothMoveTo(angles.base, angles.nod, angles.tilt, style);
      // REMOVED: delay(500) - non-blocking design

      if (expressiveness.canExpress()) {
        EmotionLabel currentEmotion = emotion.getLabel();

        if (spatialMemory.likelyHumanPresent() && random(100) < 70) {
          expressiveness.expressEmotion(currentEmotion, *servoController,
                                      emotion, personality, needs);
        } else if (random(100) < 40) {
          expressiveness.expressAgreement(*servoController, emotion, personality, needs);
        } else {
          if (personality.getCuriosity() > 0.6) {
            expressiveness.expressCuriosity(*servoController, emotion, personality, needs);
          } else {
            expressiveness.expressContemplation(*servoController, emotion, personality, needs);
          }
        }

        // REMOVED: delay(300) - non-blocking design
      }

      // ═══════════════════════════════════════════════════════════════
      // PERFORMANCE: Simplified - removed loop with delays
      // Command one attention tracking update per call
      // ═══════════════════════════════════════════════════════════════
      ServoAngles track = bodySchema.trackAttention(0.2);
      servoController->smoothMoveTo(track.base, track.nod, track.tilt, style);
      // REMOVED: delay(400) in loop - non-blocking design

      if (random(100) < 30) {
        expressiveness.applyNaturalCorrection(*servoController);
      }
    }
    // else: Reflex is active - let it handle ALL servo movements

    bodySchema.clearAttention();
    needs.satisfySocial(0.2);
    needs.consumeEnergy(0.02);
  }
  
  void executeRetreat() {
    if (animator != nullptr) {
      animator->retreatMotion(emotion, personality, needs);
    } else if (servoController != nullptr) {
      ServoAngles retreat = bodySchema.lookAt(0, 30, 15);
      MovementStyleParams style = movementGenerator.generate(emotion, personality, needs);
      servoController->smoothMoveTo(retreat.base, retreat.nod, retreat.tilt, style);
    }

    // REMOVED: delay(800) - non-blocking design
    needs.consumeEnergy(0.02);
  }

  void executeRest() {
    ServoAngles rest = bodySchema.lookAt(0, 40, 12);

    if (animator != nullptr && servoController != nullptr) {
      MovementStyleParams style = movementGenerator.generate(emotion, personality, needs);
      style.speed *= 0.6;
      servoController->smoothMoveTo(rest.base, rest.nod, rest.tilt, style);
    }

    needs.consumeEnergy(-0.1);
  }

  void executePlay() {
    // ═══════════════════════════════════════════════════════════
    // VERIFICATION: Confirm behavior is executing
    // ═══════════════════════════════════════════════════════════
    static unsigned long lastPlayLog = 0;
    if (millis() - lastPlayLog > 3000) {  // Log every 3 seconds max
      Serial.println("[BEHAVIOR] PLAY: Playful bouncing and movement");
      lastPlayLog = millis();
    }

    if (animator != nullptr) {
      animator->playfulBounce(emotion, personality, needs);
      
      if (expressiveness.canExpress() && servoController != nullptr) {
        if (emotion.getArousal() > 0.6) {
          expressiveness.expressExcitement(*servoController, emotion, personality, needs);
        } else {
          expressiveness.expressPlayfulness(*servoController, emotion, personality, needs);
        }
        delay(250);
      }
    } else {
      for (int i = 0; i < 3; i++) {
        ServoAngles play = bodySchema.exploreRandomly(20.0, 60.0);
        if (servoController != nullptr) {
          MovementStyleParams style = movementGenerator.generate(emotion, personality, needs);
          style.speed *= 1.2;
          servoController->smoothMoveTo(play.base, play.nod, play.tilt, style);
          delay(200);
        }
      }
    }
    
    needs.consumeEnergy(0.06);
  }
  
  void executeVigilant() {
    
    int hotSpots[2];
    int hotCount = attention.countHighSalienceDirections(hotSpots, 0.5);
    
    if (hotCount > 0 && servoController != nullptr) {
      MovementStyleParams style = movementGenerator.generate(emotion, personality, needs);
      
      for (int i = 0; i < hotCount; i++) {
        float dist = spatialMemory.getAverageDistance(hotSpots[i]);
        ServoAngles angles = bodySchema.lookAtDirection(hotSpots[i], dist);
        servoController->smoothMoveTo(angles.base, angles.nod, angles.tilt, style);
        delay(400);
        
        if (i == 0 && expressiveness.canExpress() && random(100) < 40) {
          if (personality.getCaution() > 0.6 || emotion.isNegative()) {
            expressiveness.expressCaution(*servoController, emotion, personality, needs);
          } else {
            expressiveness.expressUncertainty(*servoController, emotion, personality, needs);
          }
          delay(200);
        }
      }
    }
    
    needs.consumeEnergy(0.03);
  }
  
  
  Behavior getCurrentBehavior() { return currentBehavior; }
  Behavior getPreviousBehavior() { return previousBehavior; }
  float getBehaviorUncertainty() { return behaviorUncertainty; }
  
  MovementStyleParams getMovementStyle() {
    return movementGenerator.generate(emotion, personality, needs);
  }
  
  int getTargetDirection() { return attention.getFocusDirection(); }
  
  int getTargetAngle() {
    return scanner.directionToAngle(attention.getFocusDirection());
  }
  
  // ============================================
  // UPDATED: APPLY ILLUSION (uses ServoController now)
  // ============================================
  
  void applyIllusion() {
    if (animator == nullptr || servoController == nullptr) return;
    
    // Deliberation during uncertainty
    if (behaviorUncertainty > 0.7 && random(100) < 30) {
      illusion.deliberate(behaviorUncertainty, *servoController, movementGenerator,
                         emotion, personality, needs);
    }
    
    // Micro-expressions
    EmotionLabel currentEmotion = emotion.getLabel();
    if (random(100) < 25) {
      illusion.microExpression(currentEmotion, *servoController, movementGenerator,
                              emotion, personality, needs);
    }
    
    // False starts
    if (previousBehavior != currentBehavior && 
        behaviorUncertainty > 0.5 &&
        behaviorSelector.getConsecutiveCount(previousBehavior) < 2) {
      
      illusion.showIntentionConflict(previousBehavior, currentBehavior,
                                     *servoController, movementGenerator,
                                     emotion, personality, needs);
    }
    
    // Vocalization
    if (random(100) < 15) {
      illusion.vocalizeInternalState(currentEmotion);
    }
    
    // NEW: Self-correction (visible learning)
    if (random(100) < 10 && behaviorUncertainty > 0.4) {
      illusion.showSelfCorrection(*servoController, movementGenerator,
                                 emotion, personality, needs);
    }
  }
  
  void recordBehaviorOutcome(Behavior behavior, float outcome) {
    if (behavior == RETREAT && behaviorSelector.getConsecutiveCount(RETREAT) > 2) {
      outcome *= 0.5;
    }

    learningSystem.recordOutcome(behavior, outcome);
  }
  
  float calculateSessionQuality() {
    float needBalance = 1.0 - needs.getImbalance();
    float emotionalState = emotion.getValence() * 0.5 + 0.5;
    float explorationValue = spatialMemory.getTotalNovelty();
    float attentionEngagement = attention.getMaxSalience();
    
    float loopPenalty = 0.0;
    if (behaviorSelector.getConsecutiveCount(currentBehavior) > 4) {
      loopPenalty = 0.2;
    }
    
    // NEW: Bonus for goal completion
    float goalBonus = 0.0;
    if (goalSystem.hasActiveGoal()) {
      goalBonus = goalSystem.getGoalProgress() * 0.1;
    }
    
    return (needBalance * 0.3 + 
            emotionalState * 0.2 + 
            explorationValue * 0.2 +
            attentionEngagement * 0.3 +
            goalBonus -
            loopPenalty);
  }
  
  void saveState() {
    learningSystem.saveToEEPROM(personality, behaviorSelector);
  }

  void loadState() {
    learningSystem.loadFromEEPROM(personality, behaviorSelector);
  }
  
  void printFullDiagnostics() {
    Serial.println("\n╔═══════════════════════════════════════╗");
    Serial.println("║    CONSCIOUSNESS SYSTEM DIAGNOSTICS    ║");
    Serial.println("╚═══════════════════════════════════════╝");
    
    unsigned long sessionTime = (millis() - sessionStartTime) / 1000;
    Serial.print("Session uptime: ");
    Serial.print(sessionTime);
    Serial.println(" seconds");
    
    Serial.println("\n=== BODY SCHEMA ===");
    bodySchema.print();
    
    Serial.println("\n=== ATTENTION ===");
    attention.print();
    
    Serial.println("\n=== EPISODIC MEMORY ===");  // NEW
    episodicMemory.print();
    
    Serial.println("\n=== GOAL FORMATION ===");   // NEW
    goalSystem.print();
    
    Serial.println("\n=== NEEDS ===");
    needs.print();
    
    Serial.println("\n=== PERSONALITY ===");
    personality.print();
    
    Serial.println("\n=== EMOTION ===");
    emotion.print();
    
    Serial.println("\n=== SPATIAL MEMORY ===");
    spatialMemory.print();
    
    Serial.println("\n=== CURRENT BEHAVIOR ===");
    Serial.print("Active: ");
    Serial.println(behaviorToString(currentBehavior));
    Serial.print("Consecutive count: ");
    Serial.println(behaviorSelector.getConsecutiveCount(currentBehavior));
    Serial.print("Uncertainty: ");
    Serial.println(behaviorUncertainty, 2);
    
    Serial.println("\n=== BEHAVIOR STATISTICS ===");
    behaviorSelector.printWeights();

    // PACKAGE 4: Behavioral variety diagnostics
    Serial.println("\n=== BEHAVIORAL VARIETY ===");
    for (int i = 0; i < 8; i++) {
      Behavior b = (Behavior)i;
      int count = behaviorSelector.getExecutionCount(b);
      unsigned long timeSince = behaviorSelector.getTimeSinceExecution(b);

      if (count > 0) {
        Serial.print("  ");
        Serial.print(behaviorToString(b));
        Serial.print(": ");
        Serial.print(count);
        Serial.print(" times, last ");
        Serial.print(timeSince / 1000);
        Serial.println("s ago");
      }
    }

    // NEW: Person tracking diagnostics
    Serial.println("\n=== KNOWN PEOPLE ===");
    int peopleCount = 0;
    for (int i = 0; i < MAX_PEOPLE; i++) {
      if (people[i].isValid) {
        peopleCount++;
        Serial.print("  ID ");
        Serial.print(people[i].id);
        Serial.print(": ");
        Serial.print(familiarityName(people[i].familiarity));
        Serial.print(" (");
        Serial.print(people[i].interactionCount);
        Serial.print(" encounters, ");
        Serial.print(people[i].totalTimeSpent / 1000);
        Serial.println("s total)");
      }
    }
    if (peopleCount == 0) {
      Serial.println("  No people registered yet");
    }

    if (animator != nullptr) {
      Serial.println("\n=== ANIMATION STATUS ===");
      Serial.print("Currently animating: ");
      Serial.println(animator->isCurrentlyAnimating() ? "YES" : "NO");
      Serial.print("Current pose: ");
      animator->getCurrentPose().print();
    }

    consciousness.printDiagnostics();

    Serial.println("\n═══════════════════════════════════════\n");
  }

  // Getter for consciousness layer (used by AIBridge)
  ConsciousnessLayer& getConsciousness() { return consciousness; }

  // Getter for speech urge system (used by AIBridge)
  SpeechUrgeSystem& getSpeechUrge() { return speechUrge; }

  const char* behaviorToString(Behavior b) {
    switch(b) {
      case IDLE: return "IDLE";
      case EXPLORE: return "EXPLORE";
      case INVESTIGATE: return "INVESTIGATE";
      case SOCIAL_ENGAGE: return "SOCIAL_ENGAGE";
      case RETREAT: return "RETREAT";
      case REST: return "REST";
      case PLAY: return "PLAY";
      case VIGILANT: return "VIGILANT";
      default: return "UNKNOWN";
    }
  }
};

#endif // BEHAVIOR_ENGINE_H