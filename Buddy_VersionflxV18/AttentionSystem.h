// AttentionSystem.h
// Attention-driven spatial awareness and scanning control

#ifndef ATTENTION_SYSTEM_H
#define ATTENTION_SYSTEM_H

#include "SpatialMemory.h"
#include "Personality.h"

class AttentionSystem {
private:
  int focusDirection;
  float focusStrength;
  unsigned long focusStartTime;
  
  float salience[8];
  
  unsigned long lastPeripheralSweep;
  unsigned long lastFovealScan;
  unsigned long lastAmbientUpdate;
  
  const float ATTENTION_SHIFT_THRESHOLD = 0.3;
  const float FOCUS_DECAY_RATE = 0.05;
  
public:
  AttentionSystem() {
    focusDirection = 0;
    focusStrength = 0.5;
    focusStartTime = millis();
    lastPeripheralSweep = 0;
    lastFovealScan = 0;
    lastAmbientUpdate = 0;
    
    for (int i = 0; i < 8; i++) {
      salience[i] = 0.1;
    }
  }
  
  void update(SpatialMemory& memory, Personality& personality, float deltaTime) {
    updateSalience(memory, personality);
    
    int maxDir = 0;
    float maxSal = salience[0];
    
    for (int i = 1; i < 8; i++) {
      if (salience[i] > maxSal) {
        maxSal = salience[i];
        maxDir = i;
      }
    }
    
    if (maxDir != focusDirection && 
        maxSal > focusStrength + ATTENTION_SHIFT_THRESHOLD) {
      
      Serial.print("[ATTENTION] Shift: dir ");
      Serial.print(focusDirection);
      Serial.print(" → ");
      Serial.print(maxDir);
      Serial.print(" (salience: ");
      Serial.print(focusStrength, 2);
      Serial.print(" → ");
      Serial.print(maxSal, 2);
      Serial.println(")");
      
      focusDirection = maxDir;
      focusStrength = maxSal;
      focusStartTime = millis();
    }
    
    focusStrength *= exp(-FOCUS_DECAY_RATE * deltaTime);
    focusStrength = constrain(focusStrength, 0.0, 1.0);
  }
  
  void updateSalience(SpatialMemory& memory, Personality& personality) {
    for (int i = 0; i < 8; i++) {
      float novelty = memory.getNovelty(i);
      float variance = memory.getVariance(i) / 50.0;
      float recentChange = memory.getRecentChange(i) / 100.0;
      
      float distance = memory.getAverageDistance(i);
      float presenceBonus = (distance > 20 && distance < 100) ? 0.3 : 0.0;
      
      salience[i] = (
        novelty * personality.getCuriosity() * 0.4 +
        variance * personality.getExcitability() * 0.3 +
        recentChange * 0.2 +
        presenceBonus * personality.getSociability() * 0.1
      );
      
      salience[i] = constrain(salience[i], 0.0, 1.0);
    }
  }
  
  bool needsAmbientUpdate() {
    return (millis() - lastAmbientUpdate) > 500;
  }
  
  bool needsPeripheralSweep() {
    unsigned long timeSince = millis() - lastPeripheralSweep;
    
    // STARTUP: Initial baseline scan
    if (lastPeripheralSweep == 0 && millis() > 30000) {
      return true;
    }
    
    // PERIODIC: Every 7 minutes
    return timeSince > 420000;  // 7 minutes = 420,000 ms
  }
  
  bool needsFovealScan() {
    unsigned long timeSince = millis() - lastFovealScan;
    
    return (focusStrength > 0.6 && timeSince > 3000) ||
           (focusStrength > 0.4 && salience[focusDirection] > 0.7 && timeSince > 2000);
  }
  
  void markPeripheralSweep() {
    lastPeripheralSweep = millis();
  }
  
  void markFovealScan() {
    lastFovealScan = millis();
  }
  
  void markAmbientUpdate() {
    lastAmbientUpdate = millis();
  }
  
  int getFocusDirection() { return focusDirection; }
  float getFocusStrength() { return focusStrength; }
  float getSalience(int direction) {
    return (direction >= 0 && direction < 8) ? salience[direction] : 0.0;
  }

  // PACKAGE 4: Set focus direction for novelty response
  void setFocusDirection(int direction) {
    if (direction >= 0 && direction < 8) {
      focusDirection = direction;
      focusStrength = 0.7;  // Moderate strength for novelty-driven attention
      focusStartTime = millis();
    }
  }
  
  float getMaxSalience() {
    float max = salience[0];
    for (int i = 1; i < 8; i++) {
      if (salience[i] > max) max = salience[i];
    }
    return max;
  }
  
  float getTimeFocused() {
    return (millis() - focusStartTime) / 1000.0;
  }
  
  int countHighSalienceDirections(int hotSpotDirs[], float threshold = 0.6) {
    int count = 0;
    for (int i = 0; i < 8; i++) {
      if (salience[i] > threshold && count < 2) {
        hotSpotDirs[count] = i;
        count++;
      }
    }
    return count;
  }
  
  void forceAttention(int direction, float strength) {
    focusDirection = direction;
    focusStrength = strength;
    focusStartTime = millis();
    
    Serial.print("[ATTENTION] Forced to dir ");
    Serial.print(direction);
    Serial.print(" (strength: ");
    Serial.print(strength, 2);
    Serial.println(")");
  }
  
  void print() {
    Serial.println("--- ATTENTION STATE ---");
    Serial.print("  Focus direction: ");
    Serial.print(focusDirection);
    Serial.print(" (strength: ");
    Serial.print(focusStrength, 2);
    Serial.println(")");
    
    Serial.print("  Time focused: ");
    Serial.print(getTimeFocused(), 1);
    Serial.println(" seconds");
    
    Serial.println("\n  Salience map:");
    const char* dirNames[] = {"Front", "FR", "Right", "BR", "Back", "BL", "Left", "FL"};
    for (int i = 0; i < 8; i++) {
      Serial.print("    ");
      Serial.print(dirNames[i]);
      Serial.print(": ");
      printBar(salience[i]);
      if (i == focusDirection) Serial.print(" ← FOCUS");
      Serial.println();
    }
  }
  
  void printCompact() {
    Serial.print("  [ATTENTION] Focus: dir ");
    Serial.print(focusDirection);
    Serial.print(" str:");
    Serial.print(focusStrength, 2);
    Serial.print(" maxSal:");
    Serial.println(getMaxSalience(), 2);
  }
  
  void printBar(float value) {
    Serial.print("[");
    int bars = (int)(value * 10);
    for (int i = 0; i < 10; i++) {
      Serial.print(i < bars ? "█" : "░");
    }
    Serial.print("] ");
    Serial.print(value, 2);
  }
};

#endif // ATTENTION_SYSTEM_H
