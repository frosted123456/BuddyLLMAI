// MovementStyle.h
// Generates movement quality parameters from emotional state

#ifndef MOVEMENT_STYLE_H
#define MOVEMENT_STYLE_H

#include "Emotion.h"
#include "Personality.h"
#include "Needs.h"

struct MovementStyleParams {
  float speed;         // 0.0=very slow, 1.0=fast
  float amplitude;     // 0.0=small movements, 1.0=large
  float smoothness;    // 0.0=jerky, 1.0=smooth
  float directness;    // 0.0=indirect, 1.0=direct
  float hesitation;    // 0.0=confident, 1.0=many pauses
  
  // Servo timing parameters (derived)
  int delayMs;         // Movement delay in milliseconds
  int rangeScale;      // Percentage of full servo range to use
};

class MovementStyle {
public:
  MovementStyle() {}
  
  // ============================================
  // GENERATE MOVEMENT STYLE
  // ============================================
  
  MovementStyleParams generate(Emotion& emotion, Personality& personality, Needs& needs) {
    MovementStyleParams style;
    
    // === SPEED ===
    // Arousal increases speed
    style.speed = 0.3 + emotion.getArousal() * 0.7;
    
    // Low energy reduces speed
    style.speed *= (0.5 + needs.getEnergy() * 0.5);
    
    // Excitability adds speed variation
    style.speed *= (0.9 + personality.getExcitability() * 0.2);
    
    // === AMPLITUDE ===
    // Intensity and dominance increase amplitude
    style.amplitude = 0.4 + emotion.getIntensity() * 0.4 + emotion.getDominance() * 0.3;
    
    // Expressiveness increases amplitude
    style.amplitude *= (0.7 + personality.getExpressiveness() * 0.5);
    
    // Low energy reduces amplitude
    if (needs.getEnergy() < 0.4) {
      style.amplitude *= 0.6;
    }
    
    // === SMOOTHNESS ===
    // Positive emotions = smoother
    style.smoothness = 0.5 + emotion.getValence() * 0.3;
    
    // High arousal = less smooth (more jittery)
    style.smoothness -= emotion.getArousal() * 0.2;
    
    // Dominance increases smoothness (confidence)
    style.smoothness += emotion.getDominance() * 0.2;
    
    // Caution increases smoothness (controlled)
    style.smoothness += personality.getCaution() * 0.2;
    
    // === DIRECTNESS ===
    // Dominance = more direct
    style.directness = 0.4 + emotion.getDominance() * 0.6;
    
    // Curiosity reduces directness (more exploratory paths)
    style.directness -= personality.getCuriosity() * 0.2;
    
    // Low safety = less direct (evasive)
    if (needs.getSafety() < 0.5) {
      style.directness *= 0.7;
    }
    
    // === HESITATION ===
    // Caution increases hesitation
    style.hesitation = personality.getCaution() * (1.0 - emotion.getDominance());
    
    // Negative emotion increases hesitation
    if (emotion.isNegative()) {
      style.hesitation += 0.2;
    }
    
    // Low energy increases hesitation
    if (needs.getEnergy() < 0.4) {
      style.hesitation += 0.3;
    }
    
    // Clamp all values
    style.speed = constrain(style.speed, 0.1, 1.0);
    style.amplitude = constrain(style.amplitude, 0.2, 1.0);
    style.smoothness = constrain(style.smoothness, 0.2, 1.0);
    style.directness = constrain(style.directness, 0.3, 1.0);
    style.hesitation = constrain(style.hesitation, 0.0, 0.8);
    
    // === DERIVE SERVO PARAMETERS ===
    // Speed → delay (inverse relationship)
    style.delayMs = (int)(50.0 - style.speed * 45.0);  // 5-50ms range
    
    // Amplitude → range scale
    style.rangeScale = (int)(50.0 + style.amplitude * 50.0);  // 50-100% range
    
    return style;
  }
  
  // ============================================
  // APPLY STYLE TO POSITION
  // ============================================
  
  void applyToPosition(int& baseTarget, int& nodTarget, int& tiltTarget,
                       MovementStyleParams& style, 
                       int baseCenter = 90, int nodCenter = 110, int tiltCenter = 85) {
    
    // Scale movements based on amplitude
    float scale = style.amplitude;
    
    baseTarget = baseCenter + (int)((baseTarget - baseCenter) * scale);
    nodTarget = nodCenter + (int)((nodTarget - nodCenter) * scale);
    tiltTarget = tiltCenter + (int)((tiltTarget - tiltCenter) * scale);
    
    // Add slight randomness if low smoothness (jittery)
    if (style.smoothness < 0.5) {
      int jitter = (int)((0.5 - style.smoothness) * 10.0);
      baseTarget += random(-jitter, jitter + 1);
      nodTarget += random(-jitter, jitter + 1);
      tiltTarget += random(-jitter, jitter + 1);
    }
    
    // Clamp to safe ranges
    baseTarget = constrain(baseTarget, 10, 170);
    nodTarget = constrain(nodTarget, 80, 150);
    tiltTarget = constrain(tiltTarget, 20, 150);
  }
  
  // ============================================
  // EMOTION-SPECIFIC PRESETS
  // ============================================
  
  MovementStyleParams getExcitedStyle() {
    MovementStyleParams style;
    style.speed = 0.9;
    style.amplitude = 0.8;
    style.smoothness = 0.6;
    style.directness = 0.8;
    style.hesitation = 0.1;
    style.delayMs = 5;
    style.rangeScale = 90;
    return style;
  }
  
  MovementStyleParams getAnxiousStyle() {
    MovementStyleParams style;
    style.speed = 0.6;
    style.amplitude = 0.4;
    style.smoothness = 0.3;
    style.directness = 0.5;
    style.hesitation = 0.6;
    style.delayMs = 20;
    style.rangeScale = 60;
    return style;
  }
  
  MovementStyleParams getContentStyle() {
    MovementStyleParams style;
    style.speed = 0.4;
    style.amplitude = 0.5;
    style.smoothness = 0.9;
    style.directness = 0.6;
    style.hesitation = 0.2;
    style.delayMs = 30;
    style.rangeScale = 70;
    return style;
  }
  
  // ============================================
  // DIAGNOSTICS
  // ============================================
  
  void print(MovementStyleParams& style) {
    Serial.println("--- MOVEMENT STYLE ---");
    Serial.print("  Speed:       ");
    printBar(style.speed);
    Serial.print(" (delay: ");
    Serial.print(style.delayMs);
    Serial.println("ms)");
    
    Serial.print("  Amplitude:   ");
    printBar(style.amplitude);
    Serial.print(" (range: ");
    Serial.print(style.rangeScale);
    Serial.println("%)");
    
    Serial.print("  Smoothness:  ");
    printBar(style.smoothness);
    Serial.println();
    
    Serial.print("  Directness:  ");
    printBar(style.directness);
    Serial.println();
    
    Serial.print("  Hesitation:  ");
    printBar(style.hesitation);
    Serial.println();
  }
  
  void printCompact(MovementStyleParams& style) {
    Serial.print("  [STYLE] Spd:");
    Serial.print(style.speed, 1);
    Serial.print(" Amp:");
    Serial.print(style.amplitude, 1);
    Serial.print(" Smooth:");
    Serial.print(style.smoothness, 1);
    Serial.print(" Delay:");
    Serial.print(style.delayMs);
    Serial.println("ms");
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
};

#endif // MOVEMENT_STYLE_H
