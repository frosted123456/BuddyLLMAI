// AnimationController.h
// High-level animation coordinator
// REPLACES: storedAction.h, storedAction2.h, and integrates with behavior system

#ifndef ANIMATION_CONTROLLER_H
#define ANIMATION_CONTROLLER_H

#include "ServoController.h"
#include "PoseLibrary.h"
#include "BehaviorSelection.h"
#include "Emotion.h"
#include "Personality.h"
#include "MovementStyle.h"

class AnimationController {
private:
  ServoController& servos;
  PoseLibrary poseLib;
  MovementStyle movementGen;
  
  Pose currentPose;
  Behavior currentBehavior;
  
  unsigned long lastMicroMovement;
  unsigned long lastBreathing;
  bool isAnimating;
  
public:
  AnimationController(ServoController& servoController) 
    : servos(servoController) {
    
    currentPose = poseLib.getNeutralPose();
    currentBehavior = IDLE;
    lastMicroMovement = 0;
    lastBreathing = 0;
    isAnimating = false;
    
    Serial.println("[ANIMATION] Controller initialized");
  }
  
  // ============================================
  // EXECUTE BEHAVIOR ANIMATION
  // ============================================
  
  void executeBehavior(Behavior behavior, Emotion& emotion, 
                       Personality& personality, Needs& needs) {
    
    if (isAnimating) return;  // Don't interrupt ongoing animation
    
    isAnimating = true;
    currentBehavior = behavior;
    
    Serial.print("\n[ANIMATION] Executing ");
    Serial.println(behaviorToString(behavior));
    
    // Generate movement style from emotion
    MovementStyleParams style = movementGen.generate(emotion, personality, needs);
    
    if (verboseMode) {
      movementGen.printCompact(style);
    }
    
    // Generate pose sequence for this behavior
    Pose sequence[5];
    int seqLength = 0;
    poseLib.generateSequence(behavior, emotion, personality, sequence, seqLength, 5);
    
    Serial.print("  Generated sequence: ");
    Serial.print(seqLength);
    Serial.println(" poses");
    
    // Execute sequence
    for (int i = 0; i < seqLength; i++) {
      if (verboseMode) {
        Serial.print("    Pose ");
        Serial.print(i + 1);
        Serial.print("/");
        Serial.print(seqLength);
        Serial.print(": ");
        sequence[i].print();
      }
      
      servos.smoothMoveTo(sequence[i].base, sequence[i].nod, 
                         sequence[i].tilt, style);
      
      // Pause between poses (emotion-dependent)
      int pauseMs = 200 + (int)(style.hesitation * 300.0f);
      if (i < seqLength - 1) {  // Don't pause after last pose
        delay(pauseMs);
      }
    }
    
    currentPose = sequence[seqLength - 1];
    isAnimating = false;
    
    Serial.println("[ANIMATION] Sequence complete\n");
  }
  
  // ============================================
  // TRANSITION TO POSE
  // ============================================
  
  void transitionToPose(Pose& targetPose, Emotion& emotion, 
                        Personality& personality, Needs& needs) {
    
    MovementStyleParams style = movementGen.generate(emotion, personality, needs);
    
    Serial.print("[ANIMATION] Transitioning to: ");
    targetPose.print();
    
    servos.smoothMoveTo(targetPose.base, targetPose.nod, 
                       targetPose.tilt, style);
    
    currentPose = targetPose;
  }
  
  // ============================================
  // PROCEDURAL ANIMATIONS
  // ============================================
  
  void curiousTilt(Emotion& emotion, Personality& personality, Needs& needs) {
    Serial.println("[ANIMATION] Curious head tilt");
    
    MovementStyleParams style = movementGen.generate(emotion, personality, needs);
    
    int currentBase, currentNod, currentTilt;
    servos.getPosition(currentBase, currentNod, currentTilt);
    
    // Tilt in direction based on curiosity
    int tiltAmount = 20 + (int)(personality.getCuriosity() * 20.0f);
    int direction = random(0, 2) == 0 ? -1 : 1;
    
    // Tilt one way
    Pose tiltPose(currentBase, currentNod + 5, currentTilt + tiltAmount * direction);
    servos.smoothMoveTo(tiltPose.base, tiltPose.nod, tiltPose.tilt, style);
    delay(400);
    
    // Return
    Pose returnPose(currentBase, currentNod, currentTilt);
    servos.smoothMoveTo(returnPose.base, returnPose.nod, returnPose.tilt, style);
  }
  
  void scanningMotion(int centerAngle, float amplitude, Emotion& emotion, 
                      Personality& personality, Needs& needs) {
    
    Serial.println("[ANIMATION] Scanning motion");
    
    MovementStyleParams style = movementGen.generate(emotion, personality, needs);
    
    int currentBase, currentNod, currentTilt;
    servos.getPosition(currentBase, currentNod, currentTilt);
    
    int scanAmplitude = (int)(amplitude * style.amplitude);
    
    // Scan left
    Pose leftPose(centerAngle - scanAmplitude, currentNod + 5, currentTilt);
    servos.smoothMoveTo(leftPose.base, leftPose.nod, leftPose.tilt, style);
    delay(200);
    
    // Scan right
    Pose rightPose(centerAngle + scanAmplitude, currentNod + 5, currentTilt - 5);
    servos.smoothMoveTo(rightPose.base, rightPose.nod, rightPose.tilt, style);
    delay(200);
    
    // Center
    Pose centerPose(centerAngle, currentNod, currentTilt);
    servos.smoothMoveTo(centerPose.base, centerPose.nod, centerPose.tilt, style);
  }
  
  void nodYes(int count, Emotion& emotion, Personality& personality, Needs& needs) {
    Serial.print("[ANIMATION] Nodding ");
    Serial.print(count);
    Serial.println(" times");
    
    MovementStyleParams style = movementGen.generate(emotion, personality, needs);
    
    int currentBase, currentNod, currentTilt;
    servos.getPosition(currentBase, currentNod, currentTilt);
    
    int nodAmount = 15;
    
    for (int i = 0; i < count; i++) {
      // Nod down
      Pose downPose(currentBase, currentNod + nodAmount, currentTilt);
      servos.smoothMoveTo(downPose.base, downPose.nod, downPose.tilt, style);
      delay(150);
      
      // Nod up
      Pose upPose(currentBase, currentNod - 5, currentTilt);
      servos.smoothMoveTo(upPose.base, upPose.nod, upPose.tilt, style);
      delay(150);
    }
    
    // Return to neutral
    Pose neutralPose(currentBase, currentNod, currentTilt);
    servos.smoothMoveTo(neutralPose.base, neutralPose.nod, neutralPose.tilt, style);
  }
  
  void shakeNo(int count, Emotion& emotion, Personality& personality, Needs& needs) {
    Serial.print("[ANIMATION] Shaking head ");
    Serial.print(count);
    Serial.println(" times");
    
    MovementStyleParams style = movementGen.generate(emotion, personality, needs);
    
    int currentBase, currentNod, currentTilt;
    servos.getPosition(currentBase, currentNod, currentTilt);
    
    int shakeAmount = 20;
    
    for (int i = 0; i < count; i++) {
      // Shake left
      Pose leftPose(currentBase - shakeAmount, currentNod, currentTilt);
      servos.smoothMoveTo(leftPose.base, leftPose.nod, leftPose.tilt, style);
      delay(150);
      
      // Shake right
      Pose rightPose(currentBase + shakeAmount, currentNod, currentTilt);
      servos.smoothMoveTo(rightPose.base, rightPose.nod, rightPose.tilt, style);
      delay(150);
    }
    
    // Return to center
    Pose centerPose(currentBase, currentNod, currentTilt);
    servos.smoothMoveTo(centerPose.base, centerPose.nod, centerPose.tilt, style);
  }
  
  void playfulBounce(Emotion& emotion, Personality& personality, Needs& needs) {
    Serial.println("[ANIMATION] Playful bounce");
    
    MovementStyleParams style = movementGen.generate(emotion, personality, needs);
    style.speed *= 1.3f;  // Faster for bounce
    
    int currentBase, currentNod, currentTilt;
    servos.getPosition(currentBase, currentNod, currentTilt);
    
    for (int i = 0; i < 3; i++) {
      // Bounce up
      Pose upPose(currentBase + random(-10, 11), currentNod + 15, currentTilt - 10);
      servos.smoothMoveTo(upPose.base, upPose.nod, upPose.tilt, style);
      delay(100);
      
      // Bounce down
      Pose downPose(currentBase + random(-10, 11), currentNod - 5, currentTilt + 5);
      servos.smoothMoveTo(downPose.base, downPose.nod, downPose.tilt, style);
      delay(100);
    }
    
    // Return
    Pose returnPose(currentBase, currentNod, currentTilt);
    servos.smoothMoveTo(returnPose.base, returnPose.nod, returnPose.tilt, style);
  }
  
  void retreatMotion(Emotion& emotion, Personality& personality, Needs& needs) {
    Serial.println("[ANIMATION] Retreat motion");
    
    MovementStyleParams style = movementGen.generate(emotion, personality, needs);
    
    // Quick recoil
    Pose recoilPose = poseLib.getWithdrawnPose();
    servos.smoothMoveTo(recoilPose.base, recoilPose.nod, recoilPose.tilt, style);
    delay(500);
    
    // Slowly peek back
    style.speed *= 0.5f;  // Slower return
    Pose peekPose = poseLib.getNeutralPose();
    peekPose.nod -= 10;  // Still cautious
    servos.smoothMoveTo(peekPose.base, peekPose.nod, peekPose.tilt, style);
  }
  
  // ============================================
  // MICRO-MOVEMENTS (ambient life)
  // ============================================
  
  void updateMicroMovements(Behavior currentBehavior, Emotion& emotion) {
    unsigned long now = millis();
    
    // Don't add micro-movements during active animation
    if (isAnimating) return;
    
    // Breathing motion (continuous)
    if (now - lastBreathing > 100) {
      if (currentBehavior == IDLE || currentBehavior == REST) {
        // Stronger breathing when idle
        servos.breathingMotion(4.0f, 5000);
      } else {
        // Subtle breathing otherwise
        servos.breathingMotion(2.0f, 4000);
      }
      lastBreathing = now;
    }
    
    // Random micro-movements
    if (now - lastMicroMovement > 8000) {
      float microChance = 0.0f;
      
      // Probability based on behavior
      if (currentBehavior == IDLE) {
        microChance = 0.3f;
      } else if (currentBehavior == VIGILANT || currentBehavior == INVESTIGATE) {
        microChance = 0.5f;  // More attentive movements
      } else {
        microChance = 0.1f;
      }
      
      if (random(100) < (unsigned int)(microChance * 100.0f)) {
        int choice = random(0, 3);
        
        switch(choice) {
          case 0:
            servos.microTilt(emotion.getIntensity());
            break;
          case 1:
            servos.weightShift(3.0f);
            break;
          case 2:
            // Small head adjustment
            if (random(100) < 50) {
              int currentBase, currentNod, currentTilt;
              servos.getPosition(currentBase, currentNod, currentTilt);
              int newTilt = currentTilt + random(-3, 4);
              newTilt = constrain(newTilt, 20, 150);
              tiltServo.write(newTilt);
            }
            break;
        }
      }
      
      lastMicroMovement = now;
    }
  }
  
  // ============================================
  // EMOTION-DRIVEN EXPRESSIONS
  // ============================================
  
  void expressEmotion(EmotionLabel emotion, Personality& personality, Needs& needs) {
    Serial.print("[ANIMATION] Expressing emotion: ");
    Serial.println(emotionLabelToString(emotion));
    
    Emotion dummyEmotion;  // Placeholder
    MovementStyleParams style = movementGen.generate(dummyEmotion, personality, needs);
    
    Pose expressivePose;
    
    switch(emotion) {
      case EXCITED:
        expressivePose = poseLib.getExcitedPose();
        playfulBounce(dummyEmotion, personality, needs);
        break;
        
      case CURIOUS:
        expressivePose = poseLib.getCuriousTiltPose();
        servos.smoothMoveTo(expressivePose.base, expressivePose.nod, 
                           expressivePose.tilt, style);
        break;
        
      case ANXIOUS:
        expressivePose = poseLib.getWithdrawnPose();
        servos.smoothMoveTo(expressivePose.base, expressivePose.nod, 
                           expressivePose.tilt, style);
        break;
        
      case CONFUSED:
        expressivePose = poseLib.getConfusedPose();
        servos.smoothMoveTo(expressivePose.base, expressivePose.nod, 
                           expressivePose.tilt, style);
        shakeNo(2, dummyEmotion, personality, needs);
        break;
        
      case CONTENT:
        expressivePose = poseLib.getNeutralPose();
        expressivePose.nod -= 5;  // Relaxed
        servos.smoothMoveTo(expressivePose.base, expressivePose.nod, 
                           expressivePose.tilt, style);
        break;
        
      default:
        expressivePose = poseLib.getNeutralPose();
        servos.smoothMoveTo(expressivePose.base, expressivePose.nod, 
                           expressivePose.tilt, style);
        break;
    }
    
    currentPose = expressivePose;
  }
  
  // ============================================
  // UTILITY
  // ============================================
  
  void returnToNeutral(Emotion& emotion, Personality& personality, Needs& needs) {
    Serial.println("[ANIMATION] Returning to neutral");
    
    Pose neutral = poseLib.getNeutralPose();
    transitionToPose(neutral, emotion, personality, needs);
  }
  
  bool isCurrentlyAnimating() {
    return isAnimating;
  }
  
  Pose getCurrentPose() {
    return currentPose;
  }
  
  void setVerbose(bool verbose) {
    verboseMode = verbose;
  }
  
  // ============================================
  // DIAGNOSTICS
  // ============================================
  
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
  
  const char* emotionLabelToString(EmotionLabel e) {
    switch(e) {
      case NEUTRAL: return "NEUTRAL";
      case EXCITED: return "EXCITED";
      case CURIOUS: return "CURIOUS";
      case CONTENT: return "CONTENT";
      case ANXIOUS: return "ANXIOUS";
      case STARTLED: return "STARTLED";
      case BORED: return "BORED";
      case CONFUSED: return "CONFUSED";
      default: return "UNKNOWN";
    }
  }
  
private:
  bool verboseMode = true;
};

#endif // ANIMATION_CONTROLLER_H
