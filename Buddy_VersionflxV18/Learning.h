// Learning.h
// Multi-timescale learning: Fast (session), Medium (days), Slow (weeks)
// Includes EEPROM persistence

#ifndef LEARNING_H
#define LEARNING_H

#include <EEPROM.h>
#include "Personality.h"
#include "BehaviorSelection.h"

// EEPROM memory map (Teensy 4.0 has 1080 bytes)
#define EEPROM_MAGIC 0xBEEF
#define EEPROM_START_ADDR 0
#define EEPROM_VERSION 1

struct PersistentData {
  uint16_t magic;           // Validation marker
  uint8_t version;          // Data structure version
  
  // Personality traits (7 × 4 bytes = 28 bytes)
  float curiosity;
  float caution;
  float sociability;
  float playfulness;
  float excitability;
  float persistence;
  float expressiveness;
  
  // Behavior weights (8 × 4 bytes = 32 bytes)
  float behaviorWeights[8];
  
  // Session statistics
  uint32_t totalSessions;
  uint32_t totalUptime;     // seconds
  
  uint16_t checksum;        // Simple validation
};

class Learning {
private:
  // Fast weights (reset each session, not saved)
  float fastWeights[16];
  float fastDecayRate;
  
  // Medium weights (accumulate across sessions, saved)
  float mediumWeights[16];
  float mediumLearningRate;
  
  // Outcome tracking
  struct OutcomeRecord {
    float value;
    unsigned long timestamp;
  };
  OutcomeRecord recentOutcomes[10];
  int outcomeIndex;
  
  // Session info
  unsigned long sessionStart;
  uint32_t sessionCount;
  
public:
  Learning() {
    fastDecayRate = 0.90;  // per minute (was 0.95 - faster decay)
    mediumLearningRate = 0.03;  // consolidation rate (was 0.01 - 3x faster)
    outcomeIndex = 0;
    sessionStart = millis();
    sessionCount = 0;
    
    // Initialize all weights
    for (int i = 0; i < 16; i++) {
      fastWeights[i] = 0.0;
      mediumWeights[i] = 0.0;
    }
    
    for (int i = 0; i < 10; i++) {
      recentOutcomes[i].value = 0.0;
      recentOutcomes[i].timestamp = 0;
    }
  }
  
  // ============================================
  // FAST LEARNING (immediate adaptation)
  // ============================================
  
  void recordOutcome(Behavior behavior, float outcome) {
    int index = (int)behavior;
    if (index >= 0 && index < 16) {
      // Update fast weight immediately
      fastWeights[index] += outcome * 0.1;
      fastWeights[index] = constrain(fastWeights[index], -0.5, 0.5);
    }
    
    // Record in history
    recentOutcomes[outcomeIndex].value = outcome;
    recentOutcomes[outcomeIndex].timestamp = millis();
    outcomeIndex = (outcomeIndex + 1) % 10;
  }
  
  void decayFastWeights(float minutes) {
    for (int i = 0; i < 16; i++) {
      fastWeights[i] *= pow(fastDecayRate, minutes);
    }
  }
  
  // ============================================
  // MEDIUM LEARNING (consolidation)
  // ============================================
  
  void consolidate(float sessionQuality) {
    // Transfer fast learning to medium if session was good
    if (sessionQuality > 0.5) {
      for (int i = 0; i < 16; i++) {
        // Consolidate fast → medium
        float delta = fastWeights[i] * mediumLearningRate * sessionQuality;
        mediumWeights[i] += delta;
        mediumWeights[i] = constrain(mediumWeights[i], -0.3, 0.3);
      }
      
      Serial.print("[LEARNING] Consolidated weights (quality: ");
      Serial.print(sessionQuality, 2);
      Serial.println(")");
    }
  }
  
  // ============================================
  // SLOW LEARNING (personality drift)
  // ============================================
  
  float getPersonalityEvidence(const char* traitName) {
    // Calculate evidence for personality drift
    // Based on accumulated medium weights and outcomes
    
    float evidence = 0.0;
    int relevantCount = 0;
    
    // Map traits to relevant behaviors
    if (strcmp(traitName, "curiosity") == 0) {
      evidence += mediumWeights[EXPLORE] + mediumWeights[INVESTIGATE];
      relevantCount = 2;
    }
    else if (strcmp(traitName, "caution") == 0) {
      evidence += mediumWeights[RETREAT] + mediumWeights[VIGILANT];
      evidence -= mediumWeights[EXPLORE] * 0.5;
      relevantCount = 3;
    }
    else if (strcmp(traitName, "sociability") == 0) {
      evidence += mediumWeights[SOCIAL_ENGAGE];
      relevantCount = 1;
    }
    else if (strcmp(traitName, "playfulness") == 0) {
      evidence += mediumWeights[PLAY];
      relevantCount = 1;
    }
    
    return relevantCount > 0 ? evidence / relevantCount : 0.0;
  }
  
  // ============================================
  // EEPROM PERSISTENCE
  // ============================================
  
  void saveToEEPROM(Personality& personality, BehaviorSelection& behaviorSelector) {
    PersistentData data;
    
    data.magic = EEPROM_MAGIC;
    data.version = EEPROM_VERSION;
    
    // Save personality
    data.curiosity = personality.getCuriosity();
    data.caution = personality.getCaution();
    data.sociability = personality.getSociability();
    data.playfulness = personality.getPlayfulness();
    data.excitability = personality.getExcitability();
    data.persistence = personality.getPersistence();
    data.expressiveness = personality.getExpressiveness();
    
    // Save behavior weights
    for (int i = 0; i < 8; i++) {
      data.behaviorWeights[i] = behaviorSelector.getWeight(i);
    }
    
    // Save statistics
    data.totalSessions = sessionCount;
    data.totalUptime = (millis() - sessionStart) / 1000;
    
    // Calculate checksum
    data.checksum = calculateChecksum((uint8_t*)&data, sizeof(PersistentData) - 2);
    
    // Write to EEPROM
    EEPROM.put(EEPROM_START_ADDR, data);
    
    Serial.println("[EEPROM] State saved");
    Serial.print("  Sessions: ");
    Serial.println(data.totalSessions);
    Serial.print("  Uptime: ");
    Serial.print(data.totalUptime);
    Serial.println(" seconds");
  }
  
  void loadFromEEPROM(Personality& personality, BehaviorSelection& behaviorSelector) {
    PersistentData data;
    EEPROM.get(EEPROM_START_ADDR, data);
    
    // Validate
    if (data.magic != EEPROM_MAGIC) {
      Serial.println("[EEPROM] No valid data found, using defaults");
      return;
    }
    
    uint16_t calculatedChecksum = calculateChecksum((uint8_t*)&data, sizeof(PersistentData) - 2);
    if (data.checksum != calculatedChecksum) {
      Serial.println("[EEPROM] Checksum mismatch, data may be corrupted");
      return;
    }
    
    Serial.println("[EEPROM] Loading saved state...");
    
    // Restore personality
    personality.setCuriosity(data.curiosity);
    personality.setCaution(data.caution);
    personality.setSociability(data.sociability);
    personality.setPlayfulness(data.playfulness);
    personality.setExcitability(data.excitability);
    personality.setPersistence(data.persistence);
    personality.setExpressiveness(data.expressiveness);
    
    // Restore behavior weights
    for (int i = 0; i < 8; i++) {
      behaviorSelector.setWeight(i, data.behaviorWeights[i]);
    }
    
    // Restore statistics
    sessionCount = data.totalSessions + 1;  // Increment
    
    Serial.println("[EEPROM] State restored");
    Serial.print("  Previous sessions: ");
    Serial.println(data.totalSessions);
    Serial.print("  Total uptime: ");
    Serial.print(data.totalUptime);
    Serial.println(" seconds");
  }
  
  void clearEEPROM() {
    PersistentData data;
    data.magic = 0;  // Invalidate
    EEPROM.put(EEPROM_START_ADDR, data);
    Serial.println("[EEPROM] Memory cleared");
  }
  
  // ============================================
  // UTILITY
  // ============================================
  
  uint16_t calculateChecksum(uint8_t* data, int length) {
    uint16_t sum = 0;
    for (int i = 0; i < length; i++) {
      sum += data[i];
    }
    return sum;
  }
  
  float getAverageRecentOutcome() {
    float sum = 0;
    int count = 0;
    unsigned long now = millis();
    
    for (int i = 0; i < 10; i++) {
      if (recentOutcomes[i].timestamp > 0 && 
          (now - recentOutcomes[i].timestamp) < 60000) {  // Last minute
        sum += recentOutcomes[i].value;
        count++;
      }
    }
    
    return count > 0 ? sum / count : 0.0;
  }
  
  uint32_t getSessionCount() { return sessionCount; }
  
  // ============================================
  // DIAGNOSTICS
  // ============================================
  
  void print() {
    Serial.println("--- LEARNING STATE ---");
    Serial.print("  Session: ");
    Serial.println(sessionCount);
    Serial.print("  Session uptime: ");
    Serial.print((millis() - sessionStart) / 1000);
    Serial.println(" seconds");
    
    Serial.println("\n  Fast Weights (session-only):");
    for (int i = 0; i < 8; i++) {
      if (abs(fastWeights[i]) > 0.01) {
        Serial.print("    Behavior ");
        Serial.print(i);
        Serial.print(": ");
        Serial.println(fastWeights[i], 3);
      }
    }
    
    Serial.println("\n  Medium Weights (accumulated):");
    for (int i = 0; i < 8; i++) {
      if (abs(mediumWeights[i]) > 0.01) {
        Serial.print("    Behavior ");
        Serial.print(i);
        Serial.print(": ");
        Serial.println(mediumWeights[i], 3);
      }
    }
    
    Serial.print("\n  Recent outcome average: ");
    Serial.println(getAverageRecentOutcome(), 2);
    
    // NEW: Show learning effectiveness
    Serial.print("  Learning rates: fast=");
    Serial.print(fastDecayRate, 2);
    Serial.print(", medium=");
    Serial.println(mediumLearningRate, 3);
    
    Serial.print("  Total outcomes recorded: ");
    int recordedCount = 0;
    for (int i = 0; i < 10; i++) {
      if (recentOutcomes[i].timestamp > 0) recordedCount++;
    }
    Serial.println(recordedCount);
  }
};

// Now we can implement the personality drift function
void Personality::drift(Learning& learning, float driftRate) {
  // Get evidence for each trait from learning system
  float curiosityEvidence = learning.getPersonalityEvidence("curiosity");
  float cautionEvidence = learning.getPersonalityEvidence("caution");
  float sociabilityEvidence = learning.getPersonalityEvidence("sociability");
  float playfulnessEvidence = learning.getPersonalityEvidence("playfulness");
  
  // Adjust traits based on evidence
  adjustTrait(curiosity, curiosityEvidence, driftRate);
  adjustTrait(caution, cautionEvidence, driftRate);
  adjustTrait(sociability, sociabilityEvidence, driftRate);
  adjustTrait(playfulness, playfulnessEvidence, driftRate);
}

#endif // LEARNING_H