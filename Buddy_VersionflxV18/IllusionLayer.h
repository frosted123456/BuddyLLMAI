// IllusionLayer.h - UPDATED VERSION
// Creates behavioral signatures that appear as "thinking" and "feeling"
// NOW USES: ServoController for better integration with body schema

#ifndef ILLUSION_LAYER_H
#define ILLUSION_LAYER_H

#include "Emotion.h"
#include "BehaviorSelection.h"
#include "ServoController.h"
#include "MovementStyle.h"
#include "LittleBots_Board_Pins.h"

class IllusionLayer {
private:
  Behavior lastBehavior;
  EmotionLabel lastEmotion;
  
public:
  IllusionLayer() {
    lastBehavior = IDLE;
    lastEmotion = NEUTRAL;
  }
  
  // ============================================
  // SIMULATED DELIBERATION (UPDATED)
  // ============================================
  
  void deliberate(float uncertainty, ServoController& servos, MovementStyle& styleGen,
                  Emotion& emotion, Personality& personality, Needs& needs) {
    if (uncertainty < 0.3) return;
    
    int pauseMs = 300 + (int)(uncertainty * 1500);
    
    Serial.print("[DELIBERATING] Uncertainty: ");
    Serial.print(uncertainty, 2);
    Serial.print(" → pause ");
    Serial.print(pauseMs);
    Serial.println("ms");
    
    int currentBase, currentNod, currentTilt;
    servos.getPosition(currentBase, currentNod, currentTilt);
    
    // Subtle back-and-forth thinking movement
    MovementStyleParams style = styleGen.generate(emotion, personality, needs);
    style.speed *= 0.7;  // Slower for thinking
    
    for (int i = 0; i < 3; i++) {
      int newTilt = currentTilt + random(-8, 9);
      newTilt = constrain(newTilt, 20, 150);
      servos.smoothMoveTo(currentBase, currentNod, newTilt, style);
      delay(pauseMs / 3);
    }
    
    // Return to original
    servos.smoothMoveTo(currentBase, currentNod, currentTilt, style);
  }
  
  // ============================================
  // MICRO-EXPRESSIONS (UPDATED - emotional leakage)
  // ============================================
  
  void microExpression(EmotionLabel emotion, ServoController& servos,
                       MovementStyle& styleGen, Emotion& emotionState,
                       Personality& personality, Needs& needs) {
    if (emotion == lastEmotion) return;
    
    Serial.print("[MICRO-EXPRESSION] ");
    Serial.println(emotionToString(emotion));
    
    int currentBase, currentNod, currentTilt;
    servos.getPosition(currentBase, currentNod, currentTilt);
    
    MovementStyleParams style = styleGen.generate(emotionState, personality, needs);
    style.speed *= 1.5;  // Fast for micro-expressions
    
    switch(emotion) {
      case CURIOUS: {
        int newTilt = constrain(currentTilt - 12, 20, 150);
        servos.smoothMoveTo(currentBase, currentNod, newTilt, style);
        delay(180);
        servos.smoothMoveTo(currentBase, currentNod, currentTilt, style);
        break;
      }
      
      case EXCITED: {
        int newNod = constrain(currentNod + 8, 80, 150);
        servos.smoothMoveTo(currentBase, newNod, currentTilt, style);
        delay(120);
        servos.smoothMoveTo(currentBase, currentNod, currentTilt, style);
        break;
      }
      
      case ANXIOUS: {
        int newNod = constrain(currentNod - 5, 80, 150);
        servos.smoothMoveTo(currentBase, newNod, currentTilt, style);
        delay(80);
        
        int newTilt = constrain(currentTilt + 3, 20, 150);
        servos.smoothMoveTo(currentBase, newNod, newTilt, style);
        delay(80);
        
        servos.smoothMoveTo(currentBase, currentNod, currentTilt, style);
        break;
      }
      
      case STARTLED: {
        int newNod1 = constrain(currentNod - 15, 80, 150);
        servos.smoothMoveTo(currentBase, newNod1, currentTilt, style);
        delay(100);
        
        int newNod2 = constrain(currentNod - 5, 80, 150);
        servos.smoothMoveTo(currentBase, newNod2, currentTilt, style);
        delay(200);
        
        servos.smoothMoveTo(currentBase, currentNod, currentTilt, style);
        break;
      }
      
      case CONTENT: {
        int target = constrain(currentNod - 3, 80, 150);
        servos.smoothMoveTo(currentBase, target, currentTilt, style);
        delay(300);
        break;
      }
      
      case BORED: {
        int target = constrain(currentNod - 10, 80, 150);
        style.speed *= 0.5;  // Very slow for bored
        servos.smoothMoveTo(currentBase, target, currentTilt, style);
        delay(400);
        break;
      }
      
      case CONFUSED: {
        int newBase1 = constrain(currentBase - 5, 10, 170);
        servos.smoothMoveTo(newBase1, currentNod, currentTilt, style);
        delay(150);
        
        int newBase2 = constrain(currentBase + 5, 10, 170);
        servos.smoothMoveTo(newBase2, currentNod, currentTilt, style);
        delay(150);
        
        servos.smoothMoveTo(currentBase, currentNod, currentTilt, style);
        break;
      }
      
      default:
        break;
    }
    
    lastEmotion = emotion;
  }
  
  // ============================================
  // FALSE STARTS (UPDATED - intention conflicts)
  // ============================================
  
  void showIntentionConflict(Behavior rejected, Behavior chosen,
                              ServoController& servos, MovementStyle& styleGen,
                              Emotion& emotion, Personality& personality, Needs& needs) {
    if (rejected == chosen) return;
    
    Serial.print("[INTENTION CONFLICT] Considered ");
    Serial.print(behaviorToString(rejected));
    Serial.print(", chose ");
    Serial.println(behaviorToString(chosen));
    
    int currentBase, currentNod, currentTilt;
    servos.getPosition(currentBase, currentNod, currentTilt);
    
    MovementStyleParams style = styleGen.generate(emotion, personality, needs);
    style.speed *= 1.3;  // Quick false start
    
    // Brief false start movement
    switch(rejected) {
      case RETREAT: {
        // Start to lower/turn away
        int newBase = constrain(currentBase + random(-20, 21), 10, 170);
        int newNod = constrain(currentNod - 8, 80, 150);
        int newTilt = constrain(currentTilt + 10, 20, 150);
        servos.smoothMoveTo(newBase, newNod, newTilt, style);
        delay(250);
        break;
      }
      
      case INVESTIGATE: {
        // Start to lean in
        int newNod = constrain(currentNod + 10, 80, 150);
        int newTilt = constrain(currentTilt - 8, 20, 150);
        servos.smoothMoveTo(currentBase, newNod, newTilt, style);
        delay(250);
        break;
      }
      
      case EXPLORE: {
        // Start to turn
        int newBase = constrain(currentBase + random(-30, 31), 10, 170);
        servos.smoothMoveTo(newBase, currentNod, currentTilt, style);
        delay(250);
        break;
      }
      
      case SOCIAL_ENGAGE: {
        // Start to approach
        int newNod = constrain(currentNod + 5, 80, 150);
        servos.smoothMoveTo(currentBase, newNod, currentTilt, style);
        delay(200);
        break;
      }
      
      default:
        return;
    }
    
    // Correct back to original (shows change of mind)
    style.speed *= 0.8;  // Slower correction
    servos.smoothMoveTo(currentBase, currentNod, currentTilt, style);
    delay(200);
  }
  
  // ============================================
  // ATTENTIONAL DWELLING (UPDATED - studying behavior)
  // ============================================
  
  void attentionalDwell(int focusAngle, ServoController& servos,
                        MovementStyle& styleGen, Emotion& emotion,
                        Personality& personality, Needs& needs) {
    Serial.println("[PONDERING] Studying target...");
    
    int currentBase, currentNod, currentTilt;
    servos.getPosition(currentBase, currentNod, currentTilt);
    
    MovementStyleParams style = styleGen.generate(emotion, personality, needs);
    style.speed *= 0.6;  // Slow, deliberate
    
    // Small searching movements around focus
    for (int i = 0; i < 4; i++) {
      int newBase = constrain(focusAngle + random(-5, 6), 10, 170);
      int newNod = constrain(currentNod + random(-10, 11), 80, 150);
      int newTilt = constrain(currentTilt + random(-15, 16), 20, 150);
      
      servos.smoothMoveTo(newBase, newNod, newTilt, style);
      delay(random(300, 700));
    }
    
    // Return to focus point
    servos.smoothMoveTo(focusAngle, currentNod, currentTilt, style);
  }
  
  // ============================================
  // VOCALIZATION (emotional expression through sound)
  // ============================================
  
  void vocalizeInternalState(EmotionLabel emotion) {
    switch(emotion) {
      case CURIOUS: {
        // Rising questioning tone
        for (int f = 400; f < 900; f += 70) {
          tone(buzzerPin, f);
          delay(60);
        }
        noTone(buzzerPin);
        break;
      }
      
      case EXCITED: {
        // Happy ascending beeps
        int freqs[4] = {600, 800, 1000, 1200};
        for (int i = 0; i < 4; i++) {
          tone(buzzerPin, freqs[i]);
          delay(100);
        }
        noTone(buzzerPin);
        break;
      }
      
      case CONFUSED: {
        // Uncertain warbling
        for (int i = 0; i < 4; i++) {
          tone(buzzerPin, 550 + random(-100, 100));
          delay(150);
        }
        noTone(buzzerPin);
        break;
      }
      
      case CONTENT: {
        // Satisfied descending tone
        tone(buzzerPin, 900);
        delay(200);
        tone(buzzerPin, 700);
        delay(200);
        tone(buzzerPin, 500);
        delay(150);
        noTone(buzzerPin);
        break;
      }
      
      case ANXIOUS: {
        // Nervous stuttering
        for (int i = 0; i < 5; i++) {
          tone(buzzerPin, 800 + random(-200, 200));
          delay(random(80, 150));
          noTone(buzzerPin);
          delay(random(50, 100));
        }
        break;
      }
      
      case STARTLED: {
        // Sharp alarm
        tone(buzzerPin, 1500);
        delay(150);
        tone(buzzerPin, 1800);
        delay(100);
        noTone(buzzerPin);
        break;
      }
      
      case BORED: {
        // Descending sigh
        for (int f = 600; f > 300; f -= 50) {
          tone(buzzerPin, f);
          delay(120);
        }
        noTone(buzzerPin);
        break;
      }
      
      default:
        // Neutral beep
        tone(buzzerPin, 700);
        delay(100);
        noTone(buzzerPin);
        break;
    }
  }
  
  // ============================================
  // NEW: SELF-CORRECTION (visible learning)
  // ============================================
  
  void showSelfCorrection(ServoController& servos, MovementStyle& styleGen,
                          Emotion& emotion, Personality& personality, Needs& needs) {
    Serial.println("[SELF-CORRECTION] Oops, adjusting...");
    
    int currentBase, currentNod, currentTilt;
    servos.getPosition(currentBase, currentNod, currentTilt);
    
    MovementStyleParams style = styleGen.generate(emotion, personality, needs);
    
    // Small "oops" movement
    int overshootBase = constrain(currentBase + random(-8, 9), 10, 170);
    int overshootNod = constrain(currentNod + random(-5, 6), 80, 150);
    servos.smoothMoveTo(overshootBase, overshootNod, currentTilt, style);
    delay(200);
    
    // Correct back with slight hesitation
    style.hesitation += 0.2;
    servos.smoothMoveTo(currentBase, currentNod, currentTilt, style);
    
    // Little "got it" vocalization
    tone(buzzerPin, 800);
    delay(50);
    tone(buzzerPin, 1000);
    delay(80);
    noTone(buzzerPin);
  }
  
  // ============================================
  // BEHAVIOR CHANGE DETECTION
  // ============================================
  
  bool behaviorChanged(Behavior newBehavior) {
    if (newBehavior != lastBehavior) {
      lastBehavior = newBehavior;
      return true;
    }
    return false;
  }
  
  // ============================================
  // UTILITY
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
  
  const char* emotionToString(EmotionLabel e) {
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
};

#endif // ILLUSION_LAYER_H
