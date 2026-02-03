// PoseLibrary.h
// Defines behavior-specific poses and generates dynamic poses based on emotion
// REPLACES: Hard-coded arrays in storedAction.h

#ifndef POSE_LIBRARY_H
#define POSE_LIBRARY_H

#include "BehaviorSelection.h"
#include "Emotion.h"
#include "Personality.h"

struct Pose {
  int base;
  int nod;
  int tilt;
  
  Pose() : base(90), nod(110), tilt(85) {}
  Pose(int b, int n, int t) : base(b), nod(n), tilt(t) {}
  
  void print() {
    Serial.print("Base:");
    Serial.print(base);
    Serial.print("° Nod:");
    Serial.print(nod);
    Serial.print("° Tilt:");
    Serial.print(tilt);
    Serial.println("°");
  }
};

enum PoseType {
  POSE_NEUTRAL,
  POSE_ENGAGED,
  POSE_EXTREME,
  POSE_TRANSITION
};

class PoseLibrary {
private:
  
  // ============================================
  // BASE POSES PER BEHAVIOR
  // ============================================
  
  Pose getIdleBase(PoseType type) {
    switch(type) {
      case POSE_NEUTRAL:   return Pose(90, 105, 85);   // Relaxed, centered
      case POSE_ENGAGED:   return Pose(90, 110, 90);   // Slightly attentive
      case POSE_TRANSITION: return Pose(90, 108, 87);
      default:             return Pose(90, 105, 85);
    }
  }
  
  Pose getExploreBase(PoseType type) {
    switch(type) {
      case POSE_NEUTRAL:   return Pose(90, 120, 80);   // Elevated, ready
      case POSE_ENGAGED:   return Pose(135, 125, 70);  // Turned, leaning
      case POSE_EXTREME:   return Pose(170, 135, 60);  // Far turn, high
      case POSE_TRANSITION: return Pose(110, 122, 75);
      default:             return Pose(90, 120, 80);
    }
  }
  
  Pose getInvestigateBase(PoseType type) {
    switch(type) {
      case POSE_NEUTRAL:   return Pose(90, 125, 60);   // Forward lean, tilted
      case POSE_ENGAGED:   return Pose(90, 135, 45);   // Very focused, close
      case POSE_EXTREME:   return Pose(90, 140, 30);   // Maximum lean
      case POSE_TRANSITION: return Pose(90, 130, 52);
      default:             return Pose(90, 125, 60);
    }
  }
  
  Pose getSocialEngageBase(PoseType type) {
    switch(type) {
      case POSE_NEUTRAL:   return Pose(90, 120, 75);   // Open, friendly
      case POSE_ENGAGED:   return Pose(90, 125, 70);   // Attentive
      case POSE_EXTREME:   return Pose(90, 130, 65);   // Very engaged
      case POSE_TRANSITION: return Pose(90, 122, 72);
      default:             return Pose(90, 120, 75);
    }
  }
  
  Pose getRetreatBase(PoseType type) {
    switch(type) {
      case POSE_NEUTRAL:   return Pose(90, 95, 100);   // Lowered, defensive
      case POSE_ENGAGED:   return Pose(45, 85, 110);   // Turned away, low
      case POSE_EXTREME:   return Pose(10, 80, 120);   // Maximum retreat
      case POSE_TRANSITION: return Pose(70, 90, 105);
      default:             return Pose(90, 95, 100);
    }
  }
  
  Pose getRestBase(PoseType type) {
    switch(type) {
      case POSE_NEUTRAL:   return Pose(90, 100, 90);   // Lowered, relaxed
      case POSE_ENGAGED:   return Pose(90, 95, 95);    // Very relaxed
      case POSE_TRANSITION: return Pose(90, 98, 92);
      default:             return Pose(90, 100, 90);
    }
  }
  
  Pose getPlayBase(PoseType type) {
    switch(type) {
      case POSE_NEUTRAL:   return Pose(90, 115, 70);   // Bouncy, animated
      case POSE_ENGAGED:   return Pose(120, 125, 60);  // Playful turn
      case POSE_EXTREME:   return Pose(150, 130, 50);  // Exaggerated
      case POSE_TRANSITION: return Pose(105, 120, 65);
      default:             return Pose(90, 115, 70);
    }
  }
  
  Pose getVigilantBase(PoseType type) {
    switch(type) {
      case POSE_NEUTRAL:   return Pose(90, 125, 85);   // Alert, scanning
      case POSE_ENGAGED:   return Pose(90, 130, 80);   // Very alert
      case POSE_EXTREME:   return Pose(90, 135, 75);   // Maximum vigilance
      case POSE_TRANSITION: return Pose(90, 127, 82);
      default:             return Pose(90, 125, 85);
    }
  }
  
  Pose getBasePoseForBehavior(Behavior behavior, PoseType type) {
    switch(behavior) {
      case IDLE:          return getIdleBase(type);
      case EXPLORE:       return getExploreBase(type);
      case INVESTIGATE:   return getInvestigateBase(type);
      case SOCIAL_ENGAGE: return getSocialEngageBase(type);
      case RETREAT:       return getRetreatBase(type);
      case REST:          return getRestBase(type);
      case PLAY:          return getPlayBase(type);
      case VIGILANT:      return getVigilantBase(type);
      default:            return Pose(90, 110, 85);
    }
  }
  
public:
  PoseLibrary() {}
  
  // ============================================
  // DYNAMIC POSE GENERATION
  // ============================================
  
  Pose generatePose(Behavior behavior, Emotion& emotion, Personality& personality,
                    PoseType type = POSE_NEUTRAL) {
    
    // Start with base pose
    Pose pose = getBasePoseForBehavior(behavior, type);
    
    // === EMOTION MODULATION ===
    
    // Arousal affects arm height (nod angle)
    float arousalEffect = (emotion.getArousal() - 0.5f) * 20.0f;
    pose.nod += (int)arousalEffect;
    
    // Valence affects head tilt
    float valenceEffect = emotion.getValence() * 15.0f;
    pose.tilt -= (int)valenceEffect;  // Negative = tilt toward positive
    
    // Dominance affects overall height
    float dominanceEffect = (emotion.getDominance() - 0.5f) * 10.0f;
    pose.nod += (int)dominanceEffect;
    
    // === PERSONALITY MODULATION ===
    
    // Caution lowers posture
    if (personality.getCaution() > 0.6f) {
      pose.nod -= (int)((personality.getCaution() - 0.6f) * 20.0f);
    }
    
    // Curiosity adds tilt variation
    if (personality.getCuriosity() > 0.6f && behavior == INVESTIGATE) {
      pose.tilt -= (int)((personality.getCuriosity() - 0.6f) * 15.0f);
    }
    
    // Playfulness adds asymmetry
    if (personality.getPlayfulness() > 0.6f && behavior == PLAY) {
      pose.base += random(-15, 16);
      pose.tilt -= random(5, 20);
    }
    
    // === CLAMP TO SAFE RANGES ===
    pose.base = constrain(pose.base, 10, 170);
    pose.nod = constrain(pose.nod, 80, 150);
    pose.tilt = constrain(pose.tilt, 20, 150);
    
    return pose;
  }
  
  // ============================================
  // GENERATE POSE SEQUENCE
  // ============================================
  
  void generateSequence(Behavior behavior, Emotion& emotion, Personality& personality,
                        Pose sequence[], int& length, int maxLength = 5) {
    
    // Generate varied sequence based on behavior
    length = 0;
    
    switch(behavior) {
      case EXPLORE: {
        // Scanning sequence
        sequence[length++] = generatePose(behavior, emotion, personality, POSE_NEUTRAL);
        sequence[length++] = generatePose(behavior, emotion, personality, POSE_ENGAGED);
        
        // Add variation
        Pose var = generatePose(behavior, emotion, personality, POSE_ENGAGED);
        var.base += 30;
        var.base = constrain(var.base, 10, 170);
        sequence[length++] = var;
        
        var.base -= 60;
        var.base = constrain(var.base, 10, 170);
        sequence[length++] = var;
        
        sequence[length++] = generatePose(behavior, emotion, personality, POSE_NEUTRAL);
        break;
      }
      
      case INVESTIGATE: {
        // Focused examination
        sequence[length++] = generatePose(behavior, emotion, personality, POSE_NEUTRAL);
        sequence[length++] = generatePose(behavior, emotion, personality, POSE_ENGAGED);
        
        // Closer inspection
        Pose close = generatePose(behavior, emotion, personality, POSE_EXTREME);
        sequence[length++] = close;
        
        // Slight angle change
        close.base += 10;
        close.tilt -= 5;
        close.base = constrain(close.base, 10, 170);
        close.tilt = constrain(close.tilt, 20, 150);
        sequence[length++] = close;
        
        sequence[length++] = generatePose(behavior, emotion, personality, POSE_NEUTRAL);
        break;
      }
      
      case RETREAT: {
        // Defensive withdrawal
        sequence[length++] = generatePose(behavior, emotion, personality, POSE_NEUTRAL);
        sequence[length++] = generatePose(behavior, emotion, personality, POSE_ENGAGED);
        sequence[length++] = generatePose(behavior, emotion, personality, POSE_EXTREME);
        break;
      }
      
      case SOCIAL_ENGAGE: {
        // Friendly attention
        sequence[length++] = generatePose(behavior, emotion, personality, POSE_NEUTRAL);
        
        // Slight nods
        for (int i = 0; i < 2; i++) {
          Pose nod = generatePose(behavior, emotion, personality, POSE_ENGAGED);
          nod.nod += (i % 2 == 0) ? 5 : -5;
          nod.nod = constrain(nod.nod, 80, 150);
          sequence[length++] = nod;
          if (length >= maxLength) break;
        }
        break;
      }
      
      case PLAY: {
        // Animated, bouncy
        for (int i = 0; i < 4; i++) {
          Pose playful = generatePose(behavior, emotion, personality, 
                                      (i % 2 == 0) ? POSE_ENGAGED : POSE_NEUTRAL);
          playful.base += random(-20, 21);
          playful.tilt += random(-15, 16);
          playful.base = constrain(playful.base, 10, 170);
          playful.tilt = constrain(playful.tilt, 20, 150);
          sequence[length++] = playful;
          if (length >= maxLength) break;
        }
        break;
      }
      
      default: {
        // Simple neutral pose
        sequence[length++] = generatePose(behavior, emotion, personality, POSE_NEUTRAL);
        break;
      }
    }
    
    length = constrain(length, 1, maxLength);
  }
  
  // ============================================
  // SPECIAL POSES
  // ============================================
  
  Pose getNeutralPose() {
    return Pose(90, 110, 85);
  }
  
  Pose getStartupPose() {
    return Pose(90, 105, 90);
  }
  
  Pose getCuriousTiltPose() {
    return Pose(90, 120, 55);
  }
  
  Pose getConfusedPose() {
    return Pose(75, 115, 95);
  }
  
  Pose getExcitedPose() {
    return Pose(90, 135, 70);
  }
  
  Pose getWithdrawnPose() {
    return Pose(90, 90, 105);
  }
  
  // ============================================
  // INTERPOLATION
  // ============================================
  
  Pose interpolate(Pose& start, Pose& end, float t) {
    t = constrain(t, 0.0f, 1.0f);
    
    Pose result;
    result.base = start.base + (int)((end.base - start.base) * t);
    result.nod = start.nod + (int)((end.nod - start.nod) * t);
    result.tilt = start.tilt + (int)((end.tilt - start.tilt) * t);
    
    result.base = constrain(result.base, 10, 170);
    result.nod = constrain(result.nod, 80, 150);
    result.tilt = constrain(result.tilt, 20, 150);
    
    return result;
  }
  
  // ============================================
  // DIAGNOSTICS
  // ============================================
  
  void printPose(Pose& pose, const char* label = "Pose") {
    Serial.print(label);
    Serial.print(": ");
    pose.print();
  }
  
  const char* poseTypeToString(PoseType type) {
    switch(type) {
      case POSE_NEUTRAL: return "Neutral";
      case POSE_ENGAGED: return "Engaged";
      case POSE_EXTREME: return "Extreme";
      case POSE_TRANSITION: return "Transition";
      default: return "Unknown";
    }
  }
};

#endif // POSE_LIBRARY_H
