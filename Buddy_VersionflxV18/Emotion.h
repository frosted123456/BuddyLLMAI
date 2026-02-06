// Emotion.h
// Fast-changing emotional state with 3D model (arousal, valence, dominance)

#ifndef EMOTION_H
#define EMOTION_H

#include "Needs.h"
#include "Personality.h"

enum EmotionLabel {
  NEUTRAL,
  EXCITED,      // High arousal + positive valence
  CURIOUS,      // Medium arousal + positive valence
  CONTENT,      // Low arousal + positive valence
  ANXIOUS,      // High arousal + negative valence
  STARTLED,     // Very high arousal + negative valence
  BORED,        // Low arousal + negative valence
  CONFUSED      // Medium arousal + neutral/negative
};

class Emotion {
private:
  // Core dimensions (3D model)
  float arousal;      // 0.0=calm/sleepy, 1.0=alert/activated
  float valence;      // -1.0=negative, 0.0=neutral, 1.0=positive
  float dominance;    // 0.0=submissive, 1.0=confident/dominant
  
  // Emotional dynamics
  float intensity;           // 0.0=weak emotion, 1.0=strong emotion
  float baselineValence;     // Slow-changing mood
  float baselineArousal;     // General energy level
  
  float valenceVelocity;     // Rate of emotional change
  float arousalVelocity;
  
public:
  Emotion() {
    arousal = 0.5;
    valence = 0.0;
    dominance = 0.5;
    intensity = 0.3;
    baselineValence = 0.1;
    baselineArousal = 0.5;
    valenceVelocity = 0.0;
    arousalVelocity = 0.0;
  }
  
  // ============================================
  // UPDATE
  // ============================================
  
  void update(Needs& needs, Personality& personality, 
              float distance, float distanceChange, float novelty, float dt) {
    
    // === AROUSAL (activation level) ===
    float targetArousal = 0.5;
    
    // Influenced by stimulation need
    if (needs.needsStimulation()) {
      targetArousal += 0.2;  // Seeking stimulation increases arousal
    }
    
    // Influenced by energy
    targetArousal += needs.getEnergy() * 0.3;
    
    // Influenced by novelty
    targetArousal += novelty * 0.3;
    
    // Sudden changes spike arousal
    if (distanceChange > 20.0) {
      targetArousal += 0.3;
    }
    
    // Personality modulation
    targetArousal *= (0.7 + personality.getExcitability() * 0.6);
    
    // === VALENCE (positive/negative) ===
    float targetValence = 0.0;
    
    // Need satisfaction influences valence
    float needBalance = 1.0 - needs.getImbalance();
    targetValence += (needBalance - 0.5) * 0.8;  // Balanced needs = positive
    
    // Safety influences valence strongly
    if (needs.feelsThreatened()) {
      targetValence -= 0.5;
    } else {
      targetValence += (needs.getSafety() - 0.5) * 0.4;
    }
    
    // Close objects: curious if safe, anxious if not
    if (distance < 30.0 && distance > 5.0) {
      if (needs.getSafety() > 0.6) {
        targetValence += 0.2 * personality.getCuriosity();
      } else {
        targetValence -= 0.2;
      }
    }
    
    // Too close = negative
    if (distance < 10.0) {
      targetValence -= 0.3;
    }
    
    // === DOMINANCE (confidence) ===
    float targetDominance = 0.5;
    
    // High energy = more confident
    targetDominance += (needs.getEnergy() - 0.5) * 0.4;
    
    // Safety influences dominance
    targetDominance += (needs.getSafety() - 0.5) * 0.6;
    
    // Personality influences
    targetDominance += (personality.getRiskTolerance() - 0.5) * 0.3;
    targetDominance += (personality.getPersistence() - 0.5) * 0.2;
    
    // === UPDATE WITH MOMENTUM ===
    arousalVelocity = (targetArousal - arousal) * 0.3;
    valenceVelocity = (targetValence - valence) * 0.3;
    
    arousal += arousalVelocity * dt * 5.0;  // Fast response
    valence += valenceVelocity * dt * 5.0;
    dominance += (targetDominance - dominance) * dt * 3.0;  // Slower
    
    // Pull toward baseline mood (slow return to normal)
    valence += (baselineValence - valence) * 0.05 * dt;
    arousal += (baselineArousal - arousal) * 0.03 * dt;
    
    // Calculate intensity from velocity and distance from neutral
    intensity = sqrt(valenceVelocity * valenceVelocity + 
                     arousalVelocity * arousalVelocity);
    intensity += abs(valence) * 0.3 + abs(arousal - 0.5) * 0.3;
    
    // Emotional momentum — strong emotions resist change
    float emotionalMagnitude = sqrt(arousal * arousal + valence * valence);
    if (emotionalMagnitude > 0.7) {
      arousalVelocity *= 0.6;
      valenceVelocity *= 0.6;
    }

    // Emotional settling — after big change, small oscillation
    static float prevArousalVal = 0.5;
    float arousalDelta = abs(arousal - prevArousalVal);
    if (arousalDelta > 0.15) {
      arousalVelocity += (arousal - prevArousalVal) * 0.05;
    }
    prevArousalVal = arousal;

    // Clamp values
    arousal = constrain(arousal, 0.0, 1.0);
    valence = constrain(valence, -1.0, 1.0);
    dominance = constrain(dominance, 0.0, 1.0);
    intensity = constrain(intensity, 0.0, 1.0);
  }
  
  // ============================================
  // EMOTION LABELING
  // ============================================
  
  EmotionLabel getLabel() {
    // Map continuous dimensions to discrete labels
    
    if (intensity < 0.2) {
      return NEUTRAL;
    }
    
    // High arousal emotions
    if (arousal > 0.7) {
      if (valence > 0.3) {
        return EXCITED;
      } else if (valence < -0.3) {
        return (arousal > 0.85) ? STARTLED : ANXIOUS;
      } else {
        return CURIOUS;
      }
    }
    
    // Medium arousal
    if (arousal > 0.4) {
      if (valence > 0.2) {
        return CURIOUS;
      } else if (valence < -0.2) {
        return CONFUSED;
      }
    }
    
    // Low arousal emotions
    if (arousal < 0.4) {
      if (valence > 0.3) {
        return CONTENT;
      } else if (valence < -0.2) {
        return BORED;
      }
    }
    
    return NEUTRAL;
  }
  
  const char* getLabelString() {
    switch(getLabel()) {
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
  
  // ============================================
  // GETTERS
  // ============================================
  
  float getArousal() { return arousal; }
  float getValence() { return valence; }
  float getDominance() { return dominance; }
  float getIntensity() { return intensity; }
  
  bool isPositive() { return valence > 0.2; }
  bool isNegative() { return valence < -0.2; }
  bool isActivated() { return arousal > 0.6; }
  bool isCalm() { return arousal < 0.4; }
  bool isConfident() { return dominance > 0.6; }
  
  // ============================================
  // DIAGNOSTICS
  // ============================================
  
  void print() {
    Serial.println("--- EMOTION ---");
    Serial.print("  Label: ");
    Serial.println(getLabelString());
    
    Serial.print("  Arousal:   ");
    printBar(arousal);
    Serial.println();
    
    Serial.print("  Valence:   ");
    printBarSigned(valence);
    Serial.println();
    
    Serial.print("  Dominance: ");
    printBar(dominance);
    Serial.println();
    
    Serial.print("  Intensity: ");
    printBar(intensity);
    Serial.println();
    
    Serial.print("  Mood baseline: valence=");
    Serial.print(baselineValence, 2);
    Serial.print(", arousal=");
    Serial.println(baselineArousal, 2);
  }
  
  void printCompact() {
    Serial.print("  [EMOTION] ");
    Serial.print(getLabelString());
    Serial.print(" (A:");
    Serial.print(arousal, 2);
    Serial.print(" V:");
    Serial.print(valence, 2);
    Serial.print(" D:");
    Serial.print(dominance, 2);
    Serial.print(" I:");
    Serial.print(intensity, 2);
    Serial.println(")");
  }
  
  void printBar(float value) {
    Serial.print("[");
    int bars = (int)(value * 10);
    for (int i = 0; i < 10; i++) {
      if (i < bars) {
        Serial.print("█");
      } else {
        Serial.print("░");
      }
    }
    Serial.print("] ");
    Serial.print(value, 2);
  }
  
  void printBarSigned(float value) {
    // For valence (-1 to 1)
    Serial.print("[");
    int center = 5;
    int pos = (int)((value + 1.0) * 5);  // Map -1,1 to 0,10
    
    for (int i = 0; i < 10; i++) {
      if (i == center) {
        Serial.print("|");
      } else if ((value > 0 && i > center && i <= pos) ||
                 (value < 0 && i < center && i >= pos)) {
        Serial.print("█");
      } else {
        Serial.print("░");
      }
    }
    Serial.print("] ");
    Serial.print(value, 2);
  }
};

#endif // EMOTION_H