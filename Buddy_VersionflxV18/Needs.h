// Needs.h
// Homeostatic needs with FIXED safety recovery dynamics

#ifndef NEEDS_H
#define NEEDS_H

#include "Personality.h"
#include "SpatialMemory.h"

class Needs {
private:
  // Core needs (0.0 to 1.0)
  float stimulation;
  float social;
  float energy;
  float safety;
  float novelty;
  float expression;
  
  // Need dynamics — PHASE A: Slowed by 3-5x for deliberate pacing
  // Needs should take MINUTES to build meaningfully, not seconds
  float stimulationRate;
  float socialDecayRate;
  float energyCostRate;

  // Satisfaction multiplier — how impactful need satisfaction is
  static constexpr float SATISFACTION_BOOST = 1.8f;  // 1.8x more satisfying
  
  // Safety tracking (NEW)
  unsigned long lastThreatTime;
  float lastThreatMagnitude;
  int consecutiveCalmCycles;
  
  // Ideal setpoints
  const float IDEAL_STIMULATION = 0.5;
  const float IDEAL_SOCIAL = 0.4;
  const float IDEAL_ENERGY = 0.7;
  const float IDEAL_SAFETY = 0.8;
  
public:
  Needs() {
    stimulation = 0.4;
    social = 0.3;
    energy = 0.8;
    safety = 0.7;
    novelty = 0.6;
    expression = 0.5;
    
    stimulationRate = 0.0025;   // Was 0.01 — now ~5 min to go 0→0.7
    socialDecayRate = 0.00167;  // Was 0.005 — now ~3 min alone to go 0→0.7
    energyCostRate = 0.0;
    
    lastThreatTime = 0;
    lastThreatMagnitude = 0.0;
    consecutiveCalmCycles = 0;
  }
  
  void update(float deltaTime, Personality& personality, SpatialMemory& memory) {
    // STIMULATION — slowed 4x for deliberate pacing
    float environmentDynamism = memory.getAverageDynamism();
    if (environmentDynamism < 0.2) {
      stimulation -= stimulationRate * deltaTime * personality.getCuriosity();
    } else {
      stimulation += 0.005 * deltaTime * environmentDynamism;  // Was 0.02
    }

    // SOCIAL — slowed 3x
    social -= socialDecayRate * deltaTime * personality.getSociability();

    // NOVELTY — slowed 4x for gradual novelty hunger
    float totalNovelty = memory.getTotalNovelty();
    if (totalNovelty < 0.1) {
      novelty += 0.0025 * deltaTime;  // Was 0.01
    } else {
      novelty -= 0.005 * deltaTime * totalNovelty;  // Was 0.02
    }

    // ENERGY — slowed 2x (should take ~30 min to get tired)
    energy -= energyCostRate * deltaTime;
    if (energyCostRate < 0.01) {
      energy += 0.0075 * deltaTime;  // Was 0.015
    }

    // EXPRESSION — slowed 2x
    expression += 0.004 * deltaTime;  // Was 0.008
    
    // ============================================
    // SAFETY - COMPLETELY REWRITTEN
    // ============================================
    
    float maxChange = memory.getMaxRecentChange();
    unsigned long now = millis();
    
    // Check for threat
    bool threatDetected = false;
    if (maxChange > 50.0) {
      threatDetected = true;
      lastThreatTime = now;
      lastThreatMagnitude = maxChange / 100.0;  // Normalize
      consecutiveCalmCycles = 0;
      
      // Decrease safety (but less than before)
      safety -= 0.05 * lastThreatMagnitude;  // Was 0.1, now 0.05 max
      
      Serial.print("[SAFETY] Threat detected: ");
      Serial.print(maxChange);
      Serial.println(" cm change");
    } else {
      consecutiveCalmCycles++;
    }
    
    // RECOVERY: Multi-tier based on time since threat
    if (!threatDetected) {
      float timeSinceThreat = (now - lastThreatTime) / 1000.0;  // seconds
      float recoveryRate = 0.0;
      
      if (timeSinceThreat < 5.0) {
        // Just after threat: slow recovery
        recoveryRate = 0.01;
      } else if (timeSinceThreat < 15.0) {
        // Medium time: faster recovery
        recoveryRate = 0.03;
      } else {
        // Long calm period: fast recovery + bonus for consecutive calm
        recoveryRate = 0.05 + (consecutiveCalmCycles * 0.001);
      }
      
      safety += recoveryRate * deltaTime;
      
      // Habituation: if same threat level keeps appearing, reduce impact
      if (consecutiveCalmCycles > 20 && lastThreatMagnitude > 0) {
        lastThreatMagnitude *= 0.95;  // Threat becomes less scary
      }
    }
    
    // FORCE FLOOR: Never go completely to zero
    if (safety < 0.15) {
      safety = 0.15;  // Minimum safety level
      Serial.println("[SAFETY] Floor enforced at 0.15");
    }
    
    applyInteractions();
    clampNeeds();
  }
  
  void applyInteractions() {
    // Low safety suppresses social desire (but less than before)
    if (safety < 0.3) {
      social *= 0.9;  // Was 0.8
    }
    
    // Low energy reduces stimulation seeking
    if (energy < 0.3) {
      stimulation *= 0.7;
    }
    
    // High novelty is stimulating
    if (novelty > 0.7) {
      stimulation += 0.05;
    }
  }
  
  void clampNeeds() {
    stimulation = constrain(stimulation, 0.0, 1.0);
    social = constrain(social, 0.0, 1.0);
    energy = constrain(energy, 0.0, 1.0);
    safety = constrain(safety, 0.15, 1.0);  // Changed min from 0.0 to 0.15
    novelty = constrain(novelty, 0.0, 1.0);
    expression = constrain(expression, 0.0, 1.0);
  }
  
  // ============================================
  // SATISFACTION (with safety boost)
  // ============================================
  
  void satisfyStimulation(float amount) {
    stimulation += amount * SATISFACTION_BOOST;
    expression -= amount * 0.5;
    clampNeeds();
  }

  // Phase A addition: vision-driven stimulation satisfaction
  void addStimulationSatisfaction(float amount) {
    stimulation += amount * SATISFACTION_BOOST;
    clampNeeds();
  }

  void satisfySocial(float amount) {
    social += amount * SATISFACTION_BOOST;
    safety += amount * 0.15;  // Social interaction feels safe (boosted)
    clampNeeds();
  }

  void satisfyNovelty(float amount) {
    novelty -= amount * SATISFACTION_BOOST;
    stimulation += amount * 0.3;
    clampNeeds();
  }
  
  void consumeEnergy(float amount) {
    energy -= amount;
    energyCostRate = amount / 5.0;
    clampNeeds();
  }
  
  void detectHumanPresence() {
    social += 0.15;  // Boosted — human presence is more socially satisfying
    safety += 0.12;
    clampNeeds();
  }
  
  void detectThreat() {
    safety -= 0.1;  // Reduced from 0.2
    lastThreatTime = millis();
    consecutiveCalmCycles = 0;
    clampNeeds();
  }
  
  // NEW: Successful retreat restores safety
  void successfulRetreat() {
    safety += 0.3;  // Big safety boost
    lastThreatTime = millis() - 10000;  // Act like threat was 10s ago
    clampNeeds();
    
    Serial.println("[SAFETY] Successful retreat - safety restored");
  }
  
  // NEW: Force exploration when stuck
  void forceExplorationDrive() {
    stimulation = 0.3;  // Moderate stimulation need
    novelty = 0.7;      // High novelty seeking
    safety = 0.5;       // Reset safety to moderate
    
    Serial.println("[NEEDS] Exploration drive forced - breaking stuck state");
  }
  
  // ============================================
  // HOMEOSTATIC PRESSURE
  // ============================================
  
  float getStimulationPressure() {
    return abs(stimulation - IDEAL_STIMULATION);
  }
  
  float getSocialPressure() {
    return abs(social - IDEAL_SOCIAL);
  }
  
  float getEnergyPressure() {
    return abs(energy - IDEAL_ENERGY);
  }
  
  float getSafetyPressure() {
    return abs(safety - IDEAL_SAFETY);
  }
  
  float getImbalance() {
    float total = getStimulationPressure() + getSocialPressure() + 
                  getEnergyPressure() + getSafetyPressure();
    return total / 4.0;
  }
  
  // ============================================
  // GETTERS
  // ============================================
  
  float getStimulation() { return stimulation; }
  float getSocial() { return social; }
  float getEnergy() { return energy; }
  float getSafety() { return safety; }
  float getNovelty() { return novelty; }
  float getExpression() { return expression; }
  
  bool needsStimulation() { return stimulation < IDEAL_STIMULATION; }
  bool needsSocial() { return social < IDEAL_SOCIAL; }
  bool needsRest() { return energy < 0.3; }
  bool feelsThreatened() { return safety < 0.4; }  // Changed from 0.4 to be less sensitive
  bool needsNovelty() { return novelty > 0.7; }
  
  int getConsecutiveCalmCycles() { return consecutiveCalmCycles; }
  
  // ============================================
  // DIAGNOSTICS
  // ============================================
  
  void print() {
    Serial.println("--- NEEDS ---");
    Serial.print("  Stimulation: ");
    printBar(stimulation);
    Serial.print(" (pressure: ");
    Serial.print(getStimulationPressure(), 2);
    Serial.println(")");
    
    Serial.print("  Social:      ");
    printBar(social);
    Serial.print(" (pressure: ");
    Serial.print(getSocialPressure(), 2);
    Serial.println(")");
    
    Serial.print("  Energy:      ");
    printBar(energy);
    Serial.print(" (pressure: ");
    Serial.print(getEnergyPressure(), 2);
    Serial.println(")");
    
    Serial.print("  Safety:      ");
    printBar(safety);
    Serial.print(" (pressure: ");
    Serial.print(getSafetyPressure(), 2);
    Serial.print(" calm: ");
    Serial.print(consecutiveCalmCycles);
    Serial.println(")");
    
    Serial.print("  Novelty:     ");
    printBar(novelty);
    Serial.println();
    
    Serial.print("  Expression:  ");
    printBar(expression);
    Serial.println();
    
    Serial.print("  Overall imbalance: ");
    Serial.println(getImbalance(), 2);
  }
  
  void printCompact() {
    Serial.print("  [NEEDS] S:");
    Serial.print(stimulation, 1);
    Serial.print(" So:");
    Serial.print(social, 1);
    Serial.print(" E:");
    Serial.print(energy, 1);
    Serial.print(" Sa:");
    Serial.print(safety, 1);
    Serial.print(" N:");
    Serial.print(novelty, 1);
    Serial.print(" calm:");
    Serial.println(consecutiveCalmCycles);
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

#endif // NEEDS_H