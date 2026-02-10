// BehaviorSelection.h
// Behavior scoring with REPETITION PENALTIES and stuck state detection

#ifndef BEHAVIOR_SELECTION_H
#define BEHAVIOR_SELECTION_H

#include "Needs.h"
#include "Personality.h"
#include "Emotion.h"
#include "SpatialMemory.h"

// Forward declaration for memory integration
class EpisodicMemory;

enum Behavior {
  IDLE,
  EXPLORE,
  INVESTIGATE,
  SOCIAL_ENGAGE,
  RETREAT,
  REST,
  PLAY,
  VIGILANT
};

struct BehaviorScore {
  Behavior type;
  float urgency;
  float suitability;
  float expectedPayoff;
  float energyCost;
  float finalScore;
};

class BehaviorSelection {
private:
  float behaviorWeights[8];
  float successHistory[8];
  int executionCounts[8];
  
  // NEW: Anti-loop tracking
  int consecutiveExecutions[8];
  Behavior lastBehavior;
  unsigned long lastBehaviorChangeTime;
  int stuckCounter;
  
  // PACKAGE 2: Memory tracking
  unsigned long lastExecutionTime[8];  // Track recency
  int behaviorExecutionCount[8];       // Track frequency

  // PACKAGE 4: Behavioral variety tracking
  float behaviorNoveltyBonus[8];      // Track variety bonus

  // PHASE A: Behavior hysteresis — prevents rapid flip-flopping
  unsigned long behaviorDwellStart;
  static constexpr unsigned long MIN_BEHAVIOR_DWELL_MS = 10000;  // 10 seconds minimum
  static constexpr float SWITCH_THRESHOLD = 0.15f;  // Must beat current by 15%

public:
  BehaviorSelection() {
    for (int i = 0; i < 8; i++) {
      behaviorWeights[i] = 1.0;
      successHistory[i] = 0.0;
      executionCounts[i] = 0;
      consecutiveExecutions[i] = 0;
      lastExecutionTime[i] = 0;         // PACKAGE 2
      behaviorExecutionCount[i] = 0;    // PACKAGE 2
      behaviorNoveltyBonus[i] = 0.0;    // PACKAGE 4
    }

    lastBehavior = IDLE;
    lastBehaviorChangeTime = millis();
    stuckCounter = 0;
    behaviorDwellStart = millis();
  }
  
  // ============================================
  // STUCK STATE DETECTION
  // ============================================
  
  bool isStuck() {
    // Stuck if same behavior for >5 cycles AND >15 seconds
    int currentCount = consecutiveExecutions[(int)lastBehavior];
    unsigned long timeSinceChange = millis() - lastBehaviorChangeTime;
    
    if (currentCount > 5 && timeSinceChange > 15000) {
      stuckCounter++;
      
      if (stuckCounter > 2) {
        Serial.println("[STUCK DETECTION] System is stuck in loop!");
        Serial.print("  Behavior: ");
        Serial.print(behaviorToString(lastBehavior));
        Serial.print(" for ");
        Serial.print(timeSinceChange / 1000);
        Serial.println(" seconds");
        return true;
      }
    } else {
      stuckCounter = 0;
    }
    
    return false;
  }
  
  // ============================================
  // BEHAVIOR SCORING
  // ============================================
  
  int scoreAllBehaviors(Needs& needs, Personality& personality, 
                        Emotion& emotion, SpatialMemory& memory,
                        int currentDirection, BehaviorScore scores[]) {
    int index = 0;
    
    scores[index++] = scoreIdle(needs, personality, emotion);
    scores[index++] = scoreExplore(needs, personality, emotion, memory);
    scores[index++] = scoreInvestigate(needs, personality, emotion, memory, currentDirection);
    scores[index++] = scoreSocialEngage(needs, personality, emotion, memory);
    scores[index++] = scoreRetreat(needs, personality, emotion);
    scores[index++] = scoreRest(needs, personality, emotion);
    scores[index++] = scorePlay(needs, personality, emotion);
    scores[index++] = scoreVigilant(needs, personality, emotion);
    
    // NEW: Apply repetition penalty to all scores
    for (int i = 0; i < index; i++) {
      applyRepetitionPenalty(scores[i]);
    }

    // PACKAGE 4: Apply behavioral novelty bonus
    for (int i = 0; i < index; i++) {
      Behavior b = scores[i].type;
      int idx = (int)b;

      // Calculate time since last execution
      if (idx >= 0 && idx < 8 && lastExecutionTime[idx] > 0) {
        unsigned long timeSince = millis() - lastExecutionTime[idx];
        float minutesSince = timeSince / 60000.0;

        // Novelty bonus: up to +0.3 after 5 minutes
        float noveltyBonus = min(minutesSince / 5.0, 0.3);

        // Scale by curiosity (need personality reference)
        // For now, use a fixed scale
        // noveltyBonus *= personality.getCuriosity();

        behaviorNoveltyBonus[idx] = noveltyBonus;
        scores[i].finalScore += noveltyBonus;

        #if DEBUG_LEARNING
        if (noveltyBonus > 0.05) {
          Serial.print("  [VARIETY] ");
          Serial.print(behaviorToString(b));
          Serial.print(" +");
          Serial.print(noveltyBonus, 2);
          Serial.print(" (");
          Serial.print(minutesSince, 1);
          Serial.println(" min since used)");
        }
        #endif
      }
    }

    return index;
  }
  
  void applyRepetitionPenalty(BehaviorScore& score) {
    int behaviorIndex = (int)score.type;
    int consecutive = consecutiveExecutions[behaviorIndex];
    
    if (consecutive > 0) {
      // Penalty increases exponentially with repetition
      float penalty = 1.0 - (consecutive * 0.2);  // 20% per repeat
      penalty = constrain(penalty, 0.2, 1.0);  // Max 80% penalty
      
      score.finalScore *= penalty;
      
      if (consecutive > 3) {
        Serial.print("[REPETITION] ");
        Serial.print(behaviorToString(score.type));
        Serial.print(" penalty: ");
        Serial.println(penalty, 2);
      }
    }
  }
  
  BehaviorScore scoreIdle(Needs& needs, Personality& personality, Emotion& emotion) {
    BehaviorScore score;
    score.type = IDLE;
    
    score.urgency = 0.1;
    score.suitability = (1.0 - needs.getImbalance()) * (1.0 - emotion.getArousal());
    score.expectedPayoff = 0.1;
    score.energyCost = 0.0;
    
    score.finalScore = calculateFinalScore(score) * behaviorWeights[IDLE];
    return score;
  }
  
  BehaviorScore scoreExplore(Needs& needs, Personality& personality, 
                              Emotion& emotion, SpatialMemory& memory) {
    BehaviorScore score;
    score.type = EXPLORE;
    
    score.urgency = needs.needsStimulation() ? (0.5 - needs.getStimulation()) : 0.0;
    score.urgency += needs.getNovelty() * 0.3;
    
    score.suitability = personality.getEffectiveCuriosity() * needs.getEnergy();
    score.suitability *= (1.0 - personality.getCaution() * 0.3);
    
    // Boost if stuck (force exploration)
    if (needs.getConsecutiveCalmCycles() > 30) {
      score.urgency += 0.3;
    }
    
    score.expectedPayoff = 0.6;
    score.energyCost = 0.6;
    
    score.finalScore = calculateFinalScore(score) * behaviorWeights[EXPLORE];
    return score;
  }
  
  BehaviorScore scoreInvestigate(Needs& needs, Personality& personality,
                                  Emotion& emotion, SpatialMemory& memory,
                                  int currentDirection) {
    BehaviorScore score;
    score.type = INVESTIGATE;
    
    float novelty = memory.getNovelty(currentDirection);
    float change = memory.getRecentChange(currentDirection);
    
    score.urgency = novelty * 0.7 + (change > 20.0 ? 0.3 : 0.0);
    score.suitability = personality.getCuriosity() * emotion.getArousal();
    score.suitability *= (0.7 + personality.getCaution() * 0.3);
    
    score.expectedPayoff = 0.7;
    score.energyCost = 0.5;
    
    score.finalScore = calculateFinalScore(score) * behaviorWeights[INVESTIGATE];
    return score;
  }
  
  BehaviorScore scoreSocialEngage(Needs& needs, Personality& personality,
                                   Emotion& emotion, SpatialMemory& memory) {
    BehaviorScore score;
    score.type = SOCIAL_ENGAGE;
    
    bool humanDetected = memory.likelyHumanPresent();
    
    score.urgency = needs.needsSocial() ? (0.5 - needs.getSocial()) : 0.0;
    score.suitability = personality.getEffectiveSociability() * 
                        (humanDetected ? 1.0 : 0.1) *
                        (needs.getSafety() > 0.4 ? 1.0 : 0.3);  // Changed from 0.5
    
    score.expectedPayoff = 0.8;
    score.energyCost = 0.4;
    
    score.finalScore = calculateFinalScore(score) * behaviorWeights[SOCIAL_ENGAGE];
    return score;
  }
  
  BehaviorScore scoreRetreat(Needs& needs, Personality& personality, Emotion& emotion) {
    BehaviorScore score;
    score.type = RETREAT;
    
    // CHANGED: Less urgent retreat response
    score.urgency = needs.feelsThreatened() ? 0.6 : 0.0;  // Was 0.9
    score.urgency += (emotion.isNegative() && emotion.isActivated()) ? 0.3 : 0.0;  // Was 0.5
    
    score.suitability = personality.getCaution();
    
    // CHANGED: Diminishing returns on retreat
    int retreatCount = consecutiveExecutions[RETREAT];
    if (retreatCount > 2) {
      score.urgency *= 0.5;  // Urgency drops after multiple retreats
      Serial.println("[RETREAT] Diminishing urgency due to repetition");
    }
    
    score.expectedPayoff = 0.4;
    score.energyCost = 0.3;
    
    score.finalScore = calculateFinalScore(score) * behaviorWeights[RETREAT];
    return score;
  }
  
  BehaviorScore scoreRest(Needs& needs, Personality& personality, Emotion& emotion) {
    BehaviorScore score;
    score.type = REST;
    
    score.urgency = needs.needsRest() ? 0.8 : 0.0;
    score.suitability = (1.0 - needs.getEnergy()) * (1.0 - emotion.getArousal());
    score.suitability += (emotion.isPositive() && emotion.isCalm()) ? 0.3 : 0.0;
    
    // BOOST rest if stuck in negative loop
    if (consecutiveExecutions[RETREAT] > 3 || consecutiveExecutions[VIGILANT] > 3) {
      score.urgency += 0.4;
      Serial.println("[REST] Boosted to break defensive loop");
    }
    
    score.expectedPayoff = 0.5;
    score.energyCost = -0.3;
    
    score.finalScore = calculateFinalScore(score) * behaviorWeights[REST];
    return score;
  }
  
  BehaviorScore scorePlay(Needs& needs, Personality& personality, Emotion& emotion) {
    BehaviorScore score;
    score.type = PLAY;
    
    score.urgency = needs.getExpression() * 0.5;
    score.suitability = personality.getPlayfulness() * needs.getEnergy() * 
                        (emotion.isPositive() ? 1.5 : 0.5);
    
    score.expectedPayoff = 0.6;
    score.energyCost = 0.7;
    
    score.finalScore = calculateFinalScore(score) * behaviorWeights[PLAY];
    return score;
  }
  
  BehaviorScore scoreVigilant(Needs& needs, Personality& personality, Emotion& emotion) {
    BehaviorScore score;
    score.type = VIGILANT;
    
    score.urgency = (1.0 - needs.getSafety()) * 0.5;
    score.suitability = personality.getCaution() * 
                        (needs.getSafety() > 0.3 && needs.getSafety() < 0.7 ? 1.0 : 0.3);
    
    score.expectedPayoff = 0.4;
    score.energyCost = 0.3;
    
    score.finalScore = calculateFinalScore(score) * behaviorWeights[VIGILANT];
    return score;
  }
  
  float calculateFinalScore(BehaviorScore& score) {
    float combined = (
      score.urgency * 0.4 +
      score.suitability * 0.3 +
      score.expectedPayoff * 0.2 -
      score.energyCost * 0.1
    );
    
    return constrain(combined, 0.0, 1.0);
  }
  
  // ============================================
  // PACKAGE 2: MEMORY-ENHANCED BEHAVIOR SCORING
  // ============================================
  
  int scoreAllBehaviorsWithMemory(Needs& needs, Personality& personality, 
                                   Emotion& emotion, SpatialMemory& memory,
                                   int currentDirection, BehaviorScore* scores,
                                   EpisodicMemory& episodicMemory) {
    
    // First, do normal scoring
    int count = scoreAllBehaviors(needs, personality, emotion, 
                                   memory, currentDirection, scores);
    
    // Then, apply memory influence
    for (int i = 0; i < count; i++) {
      Behavior b = scores[i].type;
      
      // Check if we have experience with this behavior
      if (hasExperienceWith(episodicMemory, b)) {
        float avgOutcome = getAverageOutcome(episodicMemory, b);
        int successCount = countSuccessful(episodicMemory, b);
        
        // Memory weight: 0.5 to 1.5 multiplier
        // avgOutcome: 0.0=bad, 0.5=neutral, 1.0=good
        float memoryWeight = 0.5 + avgOutcome;
        
        // Additional boost if multiple successes
        if (successCount > 3) {
          memoryWeight += 0.1;  // Confidence bonus
        }
        
        // Apply memory influence
        float originalScore = scores[i].finalScore;
        scores[i].finalScore *= memoryWeight;
        
        Serial.print("[MEMORY] ");
        Serial.print(behaviorToString(b));
        Serial.print(": ");
        Serial.print(originalScore, 2);
        Serial.print(" → ");
        Serial.print(scores[i].finalScore, 2);
        Serial.print(" (avg outcome: ");
        Serial.print(avgOutcome, 2);
        Serial.println(")");
      }
    }
    
    // Re-sort after memory influence
    sortScores(scores, count);
    
    return count;
  }
  
  // Helper methods for memory queries (delegated to EpisodicMemory)
  bool hasExperienceWith(EpisodicMemory& mem, Behavior behavior);
  float getAverageOutcome(EpisodicMemory& mem, Behavior behavior);
  int countSuccessful(EpisodicMemory& mem, Behavior behavior);
  
  // Helper to re-sort scores
  void sortScores(BehaviorScore* scores, int count) {
    // Simple bubble sort (count is small, <10 items)
    for (int i = 0; i < count - 1; i++) {
      for (int j = 0; j < count - i - 1; j++) {
        if (scores[j].finalScore < scores[j+1].finalScore) {
          // Swap
          BehaviorScore temp = scores[j];
          scores[j] = scores[j+1];
          scores[j+1] = temp;
        }
      }
    }
  }
  
  void recordBehaviorExecution(Behavior b) {
    int idx = (int)b;
    if (idx >= 0 && idx < 8) {
      lastExecutionTime[idx] = millis();
      behaviorExecutionCount[idx]++;
      behaviorNoveltyBonus[idx] = 0.0;  // PACKAGE 4: Reset bonus after use
    }
  }
  
  unsigned long getTimeSinceExecution(Behavior b) {
    int idx = (int)b;
    if (idx >= 0 && idx < 8 && lastExecutionTime[idx] > 0) {
      return millis() - lastExecutionTime[idx];
    }
    return 999999;  // Never executed
  }
  
  int getExecutionCount(Behavior b) {
    int idx = (int)b;
    if (idx >= 0 && idx < 8) {
      return behaviorExecutionCount[idx];
    }
    return 0;
  }
  
  // ============================================
  // BEHAVIOR SELECTION
  // ============================================
  
  Behavior selectBehavior(BehaviorScore scores[], int numBehaviors) {
    int bestIndex = 0;
    float bestScore = scores[0].finalScore;

    for (int i = 1; i < numBehaviors; i++) {
      if (scores[i].finalScore > bestScore) {
        bestScore = scores[i].finalScore;
        bestIndex = i;
      }
    }

    Behavior candidateBehavior = scores[bestIndex].type;
    float candidateScore = bestScore;

    // ── PHASE A: Hysteresis — don't switch unless significantly better AND dwell met ──
    unsigned long now = millis();
    bool dwellMet = (now - behaviorDwellStart) >= MIN_BEHAVIOR_DWELL_MS;

    // Find current behavior's score
    float currentScore = 0.0f;
    for (int i = 0; i < numBehaviors; i++) {
      if (scores[i].type == lastBehavior) {
        currentScore = scores[i].finalScore;
        break;
      }
    }

    bool significantlyBetter = (candidateScore > currentScore + SWITCH_THRESHOLD);

    // Safety overrides bypass hysteresis
    bool safetyOverride = (candidateBehavior == RETREAT && candidateScore > 0.8f);

    if (safetyOverride || (dwellMet && significantlyBetter)) {
      // Allow the switch
      if (candidateBehavior != lastBehavior) {
        behaviorDwellStart = now;
      }
    } else if (!dwellMet || !significantlyBetter) {
      // Stick with current behavior
      candidateBehavior = lastBehavior;
    }

    // Small randomness (10% chance for 2nd best, only when dwell is met)
    if (dwellMet && numBehaviors > 1 && random(100) < 10) {
      int secondBest = (bestIndex == 0) ? 1 : 0;
      for (int i = 0; i < numBehaviors; i++) {
        if (i != bestIndex && scores[i].finalScore > scores[secondBest].finalScore) {
          secondBest = i;
        }
      }

      float secondScore = scores[secondBest].finalScore;
      if (secondScore > currentScore + SWITCH_THRESHOLD) {
        Serial.println("  [RANDOM] Selecting 2nd-best for variety");
        behaviorDwellStart = now;
        updateBehaviorTracking(scores[secondBest].type);
        return scores[secondBest].type;
      }
    }

    updateBehaviorTracking(candidateBehavior);
    return candidateBehavior;
  }
  
  void updateBehaviorTracking(Behavior selected) {
    // Update consecutive counters
    for (int i = 0; i < 8; i++) {
      if (i == (int)selected) {
        consecutiveExecutions[i]++;
      } else {
        consecutiveExecutions[i] = 0;
      }
    }
    
    // Track behavior changes
    if (selected != lastBehavior) {
      lastBehaviorChangeTime = millis();
      lastBehavior = selected;
    }
    
    // Record execution
    executionCounts[(int)selected]++;
  }
  
  // ============================================
  // FORCE BEHAVIOR CHANGE (emergency)
  // ============================================
  
  Behavior forceAlternativeBehavior(BehaviorScore scores[], int numBehaviors) {
    Serial.println("[FORCE] Breaking stuck loop with alternative behavior");
    
    // Find behavior that hasn't been used recently
    int bestAlternative = -1;
    float bestScore = -1.0;
    
    for (int i = 0; i < numBehaviors; i++) {
      if (scores[i].type == lastBehavior) continue;  // Skip current
      
      // Prefer behaviors with low consecutive count
      float adjustedScore = scores[i].finalScore * 
                           (1.0 + (10 - consecutiveExecutions[i]) * 0.1);
      
      if (adjustedScore > bestScore) {
        bestScore = adjustedScore;
        bestAlternative = i;
      }
    }
    
    if (bestAlternative >= 0) {
      updateBehaviorTracking(scores[bestAlternative].type);
      return scores[bestAlternative].type;
    }
    
    // Fallback: force EXPLORE
    updateBehaviorTracking(EXPLORE);
    return EXPLORE;
  }
  
  // ============================================
  // LEARNING
  // ============================================
  
  void updateWeight(Behavior behavior, float outcome) {
    int index = (int)behavior;
    
    successHistory[index] = successHistory[index] * 0.9 + outcome * 0.1;
    
    float adjustment = outcome * 0.05;
    behaviorWeights[index] += adjustment;
    behaviorWeights[index] = constrain(behaviorWeights[index], 0.3, 1.7);
  }
  
  // ============================================
  // GETTERS
  // ============================================
  
  float getWeight(int index) {
    return (index >= 0 && index < 8) ? behaviorWeights[index] : 1.0;
  }
  
  void setWeight(int index, float weight) {
    if (index >= 0 && index < 8) {
      behaviorWeights[index] = constrain(weight, 0.3, 1.7);
    }
  }
  
  int getConsecutiveCount(Behavior b) {
    return consecutiveExecutions[(int)b];
  }
  
  // ============================================
  // DIAGNOSTICS
  // ============================================
  
  void printWeights() {
    Serial.println("--- BEHAVIOR WEIGHTS ---");
    const char* names[] = {"IDLE", "EXPLORE", "INVESTIGATE", "SOCIAL", 
                           "RETREAT", "REST", "PLAY", "VIGILANT"};
    
    for (int i = 0; i < 8; i++) {
      Serial.print("  ");
      Serial.print(names[i]);
      Serial.print(": ");
      Serial.print(behaviorWeights[i], 2);
      Serial.print(" (success: ");
      Serial.print(successHistory[i], 2);
      Serial.print(", count: ");
      Serial.print(executionCounts[i]);
      Serial.print(", consecutive: ");
      Serial.print(consecutiveExecutions[i]);
      Serial.println(")");
    }
  }
  
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

// Include EpisodicMemory here to implement the memory query methods
#include "EpisodicMemory.h"

// PACKAGE 2: Implementations of memory query methods (delegated to EpisodicMemory)
inline bool BehaviorSelection::hasExperienceWith(EpisodicMemory& mem, Behavior behavior) {
  return mem.hasExperienceWith(behavior);
}

inline float BehaviorSelection::getAverageOutcome(EpisodicMemory& mem, Behavior behavior) {
  return mem.getAverageOutcome(behavior);
}

inline int BehaviorSelection::countSuccessful(EpisodicMemory& mem, Behavior behavior) {
  return mem.countSuccessful(behavior);
}

#endif // BEHAVIOR_SELECTION_H