// MovementExpression.h
// Generates VARIED emotional expressions - NOT repetitive patterns
// Replaces boring nodding with contextual, emotion-driven gestures

#ifndef MOVEMENT_EXPRESSION_H
#define MOVEMENT_EXPRESSION_H

#include "Emotion.h"
#include "Personality.h"
#include "Needs.h"
#include "ServoController.h"
#include "MovementStyle.h"

enum ExpressionType {
  EXPRESS_AGREEMENT,      // Varied agreement (not just nodding!)
  EXPRESS_CURIOSITY,      // Interest/investigation
  EXPRESS_EXCITEMENT,     // Enthusiasm
  EXPRESS_CONTEMPLATION,  // Thinking/pondering
  EXPRESS_UNCERTAINTY,    // Confusion/hesitation
  EXPRESS_AFFECTION,      // Warmth/friendliness
  EXPRESS_CAUTION,        // Wariness
  EXPRESS_PLAYFULNESS     // Fun/experimental
};

class MovementExpression {
private:
  unsigned long lastQuirk;
  unsigned long lastExpression;
  int quirkType;  // Remembers Buddy's unique quirk preference
  
  MovementStyle styleGen;
  
  // Tracks recent expressions to avoid repeating
  ExpressionType recentExpressions[5];
  int recentIndex;
  
public:
  MovementExpression() {
    lastQuirk = 0;
    lastExpression = 0;
    quirkType = random(0, 4);  // Buddy develops a preferred quirk
    recentIndex = 0;
    
    for (int i = 0; i < 5; i++) {
      recentExpressions[i] = EXPRESS_AGREEMENT;
    }
  }
  
  // ============================================
  // VARIED AGREEMENT EXPRESSIONS (not just nods!)
  // ============================================
  
  void expressAgreement(ServoController& servos, Emotion& emotion, 
                        Personality& personality, Needs& needs) {
    
    MovementStyleParams style = styleGen.generate(emotion, personality, needs);
    
    int currentBase, currentNod, currentTilt;
    servos.getPosition(currentBase, currentNod, currentTilt);
    
    // Choose varied agreement based on emotion and personality
    int choice = random(0, 4);
    
    // Avoid repeating same expression
    if (wasRecentlyUsed(EXPRESS_AGREEMENT)) {
      choice = (choice + 2) % 4;  // Pick different one
    }
    recordExpression(EXPRESS_AGREEMENT);
    
    Serial.print("[EXPRESSION] Agreement ");
    
    switch(choice) {
      case 0: {
        // Single confident nod (RARE - only 25% of time)
        Serial.println("→ Confident nod");
        Pose down(currentBase, currentNod + 20, currentTilt);
        servos.smoothMoveTo(down.base, down.nod, down.tilt, style);
        delay(180);
        
        Pose up(currentBase, currentNod - 3, currentTilt);
        servos.smoothMoveTo(up.base, up.nod, up.tilt, style);
        break;
      }
      
      case 1: {
        // Side tilt (head cocked, understanding)
        Serial.println("→ Understanding tilt");
        int tiltDir = random(0, 2) == 0 ? -1 : 1;
        Pose tilt(currentBase, currentNod + 5, currentTilt + 25 * tiltDir);
        servos.smoothMoveTo(tilt.base, tilt.nod, tilt.tilt, style);
        delay(400);
        
        Pose back(currentBase, currentNod, currentTilt);
        servos.smoothMoveTo(back.base, back.nod, back.tilt, style);
        break;
      }
      
      case 2: {
        // Lean in (engaged)
        Serial.println("→ Leaning in");
        Pose lean(currentBase, currentNod + 15, currentTilt - 10);
        servos.smoothMoveTo(lean.base, lean.nod, lean.tilt, style);
        delay(300);
        
        Pose back(currentBase, currentNod + 5, currentTilt);
        servos.smoothMoveTo(back.base, back.nod, back.tilt, style);
        break;
      }
      
      case 3: {
        // Subtle body language (minimal movement, confident)
        Serial.println("→ Subtle acknowledgment");
        Pose subtle(currentBase + random(-5, 6), currentNod + 3, currentTilt - 5);
        servos.smoothMoveTo(subtle.base, subtle.nod, subtle.tilt, style);
        delay(200);
        break;
      }
    }
  }
  
  // ============================================
  // CURIOSITY EXPRESSIONS
  // ============================================
  
  void expressCuriosity(ServoController& servos, Emotion& emotion,
                        Personality& personality, Needs& needs) {
    
    MovementStyleParams style = styleGen.generate(emotion, personality, needs);
    
    int currentBase, currentNod, currentTilt;
    servos.getPosition(currentBase, currentNod, currentTilt);
    
    if (wasRecentlyUsed(EXPRESS_CURIOSITY)) return;
    recordExpression(EXPRESS_CURIOSITY);
    
    // ═══════════════════════════════════════════════════════════════
    // PERFORMANCE: Simplified non-blocking expression
    // Command one curiosity pose and return immediately
    // ═══════════════════════════════════════════════════════════════
    int choice = random(0, 3);
    Serial.print("[EXPRESSION] Curiosity ");

    switch(choice) {
      case 0: {
        // Inquisitive tilt + lean
        Serial.println("→ Inquisitive lean");
        int tiltDir = random(0, 2) == 0 ? -1 : 1;
        Pose inquiry(currentBase, currentNod + 12, currentTilt + 30 * tiltDir);
        servos.smoothMoveTo(inquiry.base, inquiry.nod, inquiry.tilt, style);
        // REMOVED: delay(600) and return movement - non-blocking design
        break;
      }

      case 1: {
        // Slight turn + study
        Serial.println("→ Study turn");
        Pose turn(currentBase + random(-20, 20), currentNod + 10, currentTilt - 15);
        servos.smoothMoveTo(turn.base, turn.nod, turn.tilt, style);
        // REMOVED: delay(500), adjust movement, delay(300), and return - non-blocking
        break;
      }

      case 2: {
        // Peek and inspect
        Serial.println("→ Peek behavior");
        Pose peek(currentBase + random(-15, 15), currentNod + 18, currentTilt - 20);
        servos.smoothMoveTo(peek.base, peek.nod, peek.tilt, style);
        // REMOVED: delay(400) and return movement - non-blocking design
        break;
      }
    }
  }
  
  // ============================================
  // EXCITEMENT EXPRESSIONS
  // ============================================
  
  void expressExcitement(ServoController& servos, Emotion& emotion,
                         Personality& personality, Needs& needs) {
    
    MovementStyleParams style = styleGen.generate(emotion, personality, needs);
    style.speed *= 1.4;  // Faster for excitement
    
    int currentBase, currentNod, currentTilt;
    servos.getPosition(currentBase, currentNod, currentTilt);
    
    if (wasRecentlyUsed(EXPRESS_EXCITEMENT)) return;
    recordExpression(EXPRESS_EXCITEMENT);
    
    Serial.println("[EXPRESSION] Excitement → Bouncy movement");
    
    // Quick bouncy sequence
    for (int i = 0; i < 2; i++) {
      Pose up(currentBase + random(-10, 10), currentNod + 15, currentTilt - 10);
      servos.smoothMoveTo(up.base, up.nod, up.tilt, style);
      delay(100);
      
      Pose down(currentBase + random(-10, 10), currentNod - 5, currentTilt + 5);
      servos.smoothMoveTo(down.base, down.nod, down.tilt, style);
      delay(100);
    }
    
    Pose settle(currentBase, currentNod + 5, currentTilt);
    servos.smoothMoveTo(settle.base, settle.nod, settle.tilt, style);
  }
  
  // ============================================
  // CONTEMPLATION EXPRESSIONS
  // ============================================
  
  void expressContemplation(ServoController& servos, Emotion& emotion,
                            Personality& personality, Needs& needs) {
    
    MovementStyleParams style = styleGen.generate(emotion, personality, needs);
    style.speed *= 0.7;  // Slower, thoughtful
    
    int currentBase, currentNod, currentTilt;
    servos.getPosition(currentBase, currentNod, currentTilt);
    
    if (wasRecentlyUsed(EXPRESS_CONTEMPLATION)) return;
    recordExpression(EXPRESS_CONTEMPLATION);
    
    // ═══════════════════════════════════════════════════════════════
    // PERFORMANCE: Simplified non-blocking expression
    // ═══════════════════════════════════════════════════════════════
    int choice = random(0, 2);
    Serial.print("[EXPRESSION] Contemplation ");

    switch(choice) {
      case 0: {
        // Slow turn away
        Serial.println("→ Thoughtful turn");
        int turnDir = random(0, 2) == 0 ? -1 : 1;
        Pose away(currentBase + 25 * turnDir, currentNod + 5, currentTilt + 10 * turnDir);
        servos.smoothMoveTo(away.base, away.nod, away.tilt, style);
        // REMOVED: delay(700) and return movement - non-blocking design
        break;
      }

      case 1: {
        // Lower gaze
        Serial.println("→ Pensive gaze");
        Pose down(currentBase, currentNod - 8, currentTilt + 5);
        servos.smoothMoveTo(down.base, down.nod, down.tilt, style);
        // REMOVED: delay(800) and lift movement - non-blocking design
        break;
      }
    }
  }
  
  // ============================================
  // AFFECTION/WARMTH EXPRESSIONS
  // ============================================
  
  void expressAffection(ServoController& servos, Emotion& emotion,
                        Personality& personality, Needs& needs) {
    
    MovementStyleParams style = styleGen.generate(emotion, personality, needs);
    
    int currentBase, currentNod, currentTilt;
    servos.getPosition(currentBase, currentNod, currentTilt);
    
    if (wasRecentlyUsed(EXPRESS_AFFECTION)) return;
    recordExpression(EXPRESS_AFFECTION);
    
    int choice = random(0, 3);
    Serial.print("[EXPRESSION] Affection ");
    
    switch(choice) {
      case 0: {
        // Gentle sway
        Serial.println("→ Gentle sway");
        Pose left(currentBase - 8, currentNod + 3, currentTilt + 10);
        servos.smoothMoveTo(left.base, left.nod, left.tilt, style);
        delay(300);
        
        Pose right(currentBase + 8, currentNod + 3, currentTilt - 10);
        servos.smoothMoveTo(right.base, right.nod, right.tilt, style);
        delay(300);
        
        Pose center(currentBase, currentNod, currentTilt);
        servos.smoothMoveTo(center.base, center.nod, center.tilt, style);
        break;
      }
      
      case 1: {
        // Soft tilt toward
        Serial.println("→ Warm tilt");
        int tiltDir = random(0, 2) == 0 ? -1 : 1;
        Pose tilt(currentBase, currentNod + 8, currentTilt + 20 * tiltDir);
        servos.smoothMoveTo(tilt.base, tilt.nod, tilt.tilt, style);
        delay(500);
        
        Pose back(currentBase, currentNod, currentTilt);
        servos.smoothMoveTo(back.base, back.nod, back.tilt, style);
        break;
      }
      
      case 2: {
        // Settle closer
        Serial.println("→ Settle near");
        Pose close(currentBase, currentNod + 10, currentTilt - 5);
        servos.smoothMoveTo(close.base, close.nod, close.tilt, style);
        delay(400);
        break;
      }
    }
  }
  
  // ============================================
  // PERSONALITY QUIRKS (Buddy's signature moves)
  // ============================================
  
  void performQuirk(ServoController& servos, Personality& personality, Needs& needs) {
    unsigned long now = millis();
    
    // Quirks happen every 15-25 seconds
    int quirkInterval = 15000 + (int)(personality.getPlayfulness() * 10000);
    if (now - lastQuirk < quirkInterval) return;
    
    lastQuirk = now;
    
    int currentBase, currentNod, currentTilt;
    servos.getPosition(currentBase, currentNod, currentTilt);
    
    Serial.print("[QUIRK] Personality signature #");
    Serial.println(quirkType);
    
    // Each Buddy develops a preferred quirk
    switch(quirkType) {
      case 0: {
        // "The Thinker" - touches "chin" (nods down, tilts)
        Pose think(currentBase, currentNod + 15, currentTilt + 20);
        servos.snapTo(think.base, think.nod, think.tilt);
        delay(600);
        
        Pose up(currentBase, currentNod, currentTilt);
        servos.snapTo(up.base, up.nod, up.tilt);
        break;
      }
      
      case 1: {
        // "The Watcher" - quick turn and hold
        int dir = random(0, 2) == 0 ? -1 : 1;
        Pose watch(currentBase + 30 * dir, currentNod + 10, currentTilt - 10 * dir);
        servos.snapTo(watch.base, watch.nod, watch.tilt);
        delay(500);
        
        Pose back(currentBase, currentNod, currentTilt);
        servos.snapTo(back.base, back.nod, back.tilt);
        break;
      }
      
      case 2: {
        // "The Wobbler" - subtle side-to-side
        for (int i = 0; i < 3; i++) {
          Pose wobble(currentBase + random(-5, 6), currentNod, 
                      currentTilt + random(-8, 8));
          servos.snapTo(wobble.base, wobble.nod, wobble.tilt);
          delay(200);
        }
        break;
      }
      
      case 3: {
        // "The Stargazer" - looks up periodically
        Pose up(currentBase, currentNod + 25, currentTilt);
        servos.snapTo(up.base, up.nod, up.tilt);
        delay(700);
        
        Pose down(currentBase, currentNod, currentTilt);
        servos.snapTo(down.base, down.nod, down.tilt);
        break;
      }
    }
  }
  
  // ============================================
  // ANTICIPATION/WINDUP (shows intention)
  // ============================================
  
  void anticipateMovement(ServoController& servos, int targetBase, int targetNod,
                          Emotion& emotion, Personality& personality) {
    
    int currentBase, currentNod, currentTilt;
    servos.getPosition(currentBase, currentNod, currentTilt);
    
    // Brief "windup" in opposite direction
    int baseDir = (targetBase > currentBase) ? -1 : 1;
    int nodDir = (targetNod > currentNod) ? -1 : 1;
    
    Pose windup(
      currentBase + baseDir * 8,
      currentNod + nodDir * 5,
      currentTilt + random(-5, 5)
    );
    
    Serial.println("[ANTICIPATION] Subtle windup");
    servos.snapTo(windup.base, windup.nod, windup.tilt);
    delay(100);
  }
  
  // ============================================
  // NATURAL CORRECTIONS (overshoot/settle)
  // ============================================
  
  void applyNaturalCorrection(ServoController& servos) {
    if (random(100) > 30) return;  // 30% of time

    int currentBase, currentNod, currentTilt;
    servos.getPosition(currentBase, currentNod, currentTilt);

    // ═══════════════════════════════════════════════════════════════
    // PERFORMANCE: Simplified non-blocking correction
    // Just apply small overshoot, no delay or settle
    // ═══════════════════════════════════════════════════════════════
    Pose overshoot(
      currentBase + random(-5, 6),
      currentNod + random(-3, 4),
      currentTilt + random(-5, 6)
    );

    Serial.println("[CORRECTION] Natural settle");
    servos.snapTo(overshoot.base, overshoot.nod, overshoot.tilt);
    // REMOVED: delay(80) and settle snap - non-blocking design
  }
  
  // ============================================
  // EMOTION-BASED EXPRESSION SELECTOR
  // ============================================
  
  void expressEmotion(EmotionLabel emotion, ServoController& servos,
                      Emotion& emotionState, Personality& personality, Needs& needs) {
    
    switch(emotion) {
      case EXCITED:
        expressExcitement(servos, emotionState, personality, needs);
        break;
        
      case CURIOUS:
        expressCuriosity(servos, emotionState, personality, needs);
        break;
        
      case CONTENT:
        expressAffection(servos, emotionState, personality, needs);
        break;
        
      case CONFUSED:
        expressContemplation(servos, emotionState, personality, needs);
        break;
        
      case ANXIOUS:
        // No extra movement when anxious (already handled by retreat)
        break;
        
      default:
        // Random choice for neutral emotions
        int choice = random(0, 3);
        switch(choice) {
          case 0:
            expressAgreement(servos, emotionState, personality, needs);
            break;
          case 1:
            expressCuriosity(servos, emotionState, personality, needs);
            break;
          case 2:
            expressContemplation(servos, emotionState, personality, needs);
            break;
        }
        break;
    }
  }
  
  // ============================================
  // PLAYFULNESS EXPRESSIONS
  // ============================================
  
  void expressPlayfulness(ServoController& servos, Emotion& emotion,
                         Personality& personality, Needs& needs) {
    
    MovementStyleParams style = styleGen.generate(emotion, personality, needs);
    style.amplitude *= 1.3;  // Bigger movements
    style.speed *= 1.2;      // Faster
    
    int currentBase, currentNod, currentTilt;
    servos.getPosition(currentBase, currentNod, currentTilt);
    
    if (wasRecentlyUsed(EXPRESS_PLAYFULNESS)) return;
    recordExpression(EXPRESS_PLAYFULNESS);
    
    Serial.println("[EXPRESSION] Playfulness → Bouncy animation");
    
    // Bouncy, animated movement
    for (int i = 0; i < 3; i++) {
      int newBase = currentBase + random(-20, 21);
      int newNod = currentNod + random(-10, 11);
      int newTilt = currentTilt + random(-15, 16);
      
      newBase = constrain(newBase, 10, 170);
      newNod = constrain(newNod, 80, 150);
      newTilt = constrain(newTilt, 20, 150);
      
      Pose playPose(newBase, newNod, newTilt);
      servos.smoothMoveTo(playPose.base, playPose.nod, playPose.tilt, style);
      delay(150);
    }
    
    // Return to original
    Pose back(currentBase, currentNod, currentTilt);
    servos.smoothMoveTo(back.base, back.nod, back.tilt, style);
  }
  
  // ============================================
  // CAUTION EXPRESSIONS
  // ============================================
  
  void expressCaution(ServoController& servos, Emotion& emotion,
                     Personality& personality, Needs& needs) {
    
    MovementStyleParams style = styleGen.generate(emotion, personality, needs);
    style.speed *= 0.6;       // Slower
    style.hesitation += 0.3;  // More hesitant
    
    int currentBase, currentNod, currentTilt;
    servos.getPosition(currentBase, currentNod, currentTilt);
    
    if (wasRecentlyUsed(EXPRESS_CAUTION)) return;
    recordExpression(EXPRESS_CAUTION);
    
    Serial.println("[EXPRESSION] Caution → Careful scanning");
    
    // Slow, careful scanning
    int leftBase = constrain(currentBase - 15, 10, 170);
    Pose left(leftBase, currentNod, currentTilt);
    servos.smoothMoveTo(left.base, left.nod, left.tilt, style);
    delay(300);
    
    int rightBase = constrain(currentBase + 15, 10, 170);
    Pose right(rightBase, currentNod, currentTilt);
    servos.smoothMoveTo(right.base, right.nod, right.tilt, style);
    delay(300);
    
    // Return to center
    Pose center(currentBase, currentNod, currentTilt);
    servos.smoothMoveTo(center.base, center.nod, center.tilt, style);
  }
  
  // ============================================
  // UNCERTAINTY EXPRESSIONS
  // ============================================
  
  void expressUncertainty(ServoController& servos, Emotion& emotion,
                         Personality& personality, Needs& needs) {
    
    MovementStyleParams style = styleGen.generate(emotion, personality, needs);
    style.smoothness *= 0.5;  // Jerkier
    
    int currentBase, currentNod, currentTilt;
    servos.getPosition(currentBase, currentNod, currentTilt);
    
    if (wasRecentlyUsed(EXPRESS_UNCERTAINTY)) return;
    recordExpression(EXPRESS_UNCERTAINTY);
    
    Serial.println("[EXPRESSION] Uncertainty → Hesitant movements");
    
    // Small hesitant movements
    for (int i = 0; i < 4; i++) {
      int newTilt = currentTilt + random(-5, 6);
      int newNod = currentNod + random(-3, 4);
      
      newTilt = constrain(newTilt, 20, 150);
      newNod = constrain(newNod, 80, 150);
      
      Pose uncertain(currentBase, newNod, newTilt);
      servos.smoothMoveTo(uncertain.base, uncertain.nod, uncertain.tilt, style);
      delay(random(150, 300));
    }
    
    // Return
    Pose back(currentBase, currentNod, currentTilt);
    servos.smoothMoveTo(back.base, back.nod, back.tilt, style);
  }
  
  // ============================================
  // UTILITY
  // ============================================
  
  bool wasRecentlyUsed(ExpressionType type) {
    for (int i = 0; i < 5; i++) {
      if (recentExpressions[i] == type) return true;
    }
    return false;
  }
  
  void recordExpression(ExpressionType type) {
    recentExpressions[recentIndex] = type;
    recentIndex = (recentIndex + 1) % 5;
    lastExpression = millis();
  }
  
  bool canExpress() {
    // Don't spam expressions - minimum 2 seconds between
    return (millis() - lastExpression) > 2000;
  }
  
  void resetQuirkTimer() {
    lastQuirk = millis();
  }
};

#endif // MOVEMENT_EXPRESSION_H
