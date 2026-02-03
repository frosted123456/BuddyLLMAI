// OutcomeCalculator.h
// Standardized outcome measurement for learning
// Combines multiple factors into consistent 0.0-1.0 score

#ifndef OUTCOME_CALCULATOR_H
#define OUTCOME_CALCULATOR_H

#include "Needs.h"
#include "Emotion.h"
#include "GoalFormation.h"
#include "BehaviorSelection.h"

class OutcomeCalculator {
private:
  // Snapshots for comparison (individual values, not objects)
  float needsSnapshot_stimulation;
  float needsSnapshot_social;
  float needsSnapshot_energy;
  float needsSnapshot_safety;
  float needsSnapshot_novelty;
  float emotionSnapshot_arousal;
  float emotionSnapshot_valence;
  unsigned long startTime;

  // Weights for different factors
  const float WEIGHT_NEEDS = 0.40;
  const float WEIGHT_EMOTION = 0.30;
  const float WEIGHT_GOAL = 0.20;
  const float WEIGHT_SAFETY = 0.10;

public:
  OutcomeCalculator() {
    needsSnapshot_stimulation = 0.5;
    needsSnapshot_social = 0.5;
    needsSnapshot_energy = 0.5;
    needsSnapshot_safety = 0.5;
    needsSnapshot_novelty = 0.5;
    emotionSnapshot_arousal = 0.5;
    emotionSnapshot_valence = 0.5;
    startTime = 0;
  }

  // ============================================
  // SNAPSHOT STATE BEFORE BEHAVIOR
  // ============================================

  void snapshotState(Needs& needs, Emotion& emotion) {
    // Capture individual values (can't copy objects with const members)
    needsSnapshot_stimulation = needs.getStimulation();
    needsSnapshot_social = needs.getSocial();
    needsSnapshot_energy = needs.getEnergy();
    needsSnapshot_safety = needs.getSafety();
    needsSnapshot_novelty = needs.getNovelty();
    emotionSnapshot_arousal = emotion.getArousal();
    emotionSnapshot_valence = emotion.getValence();
    startTime = millis();
  }

  // ============================================
  // CALCULATE OUTCOME AFTER BEHAVIOR
  // ============================================

  float calculate(Behavior behavior, Needs& needsAfter, Emotion& emotionAfter,
                  GoalFormation* goalSystem = nullptr) {

    float outcome = 0.5;  // Neutral baseline

    // ========================================
    // FACTOR 1: Need Satisfaction (40%)
    // ========================================

    float needImprovement = calculateNeedImprovement(behavior, needsAfter);
    outcome += needImprovement * WEIGHT_NEEDS;

    // ========================================
    // FACTOR 2: Emotional Improvement (30%)
    // ========================================

    float emotionImprovement = calculateEmotionalImprovement(emotionAfter);
    outcome += emotionImprovement * WEIGHT_EMOTION;

    // ========================================
    // FACTOR 3: Goal Alignment (20%)
    // ========================================

    if (goalSystem != nullptr && goalSystem->hasActiveGoal()) {
      float goalAlignment = calculateGoalAlignment(behavior, goalSystem);
      outcome += goalAlignment * WEIGHT_GOAL;
    }

    // ========================================
    // FACTOR 4: Safety Maintenance (10%)
    // ========================================

    float safetyMaintenance = calculateSafetyMaintenance(behavior, needsAfter);
    outcome += safetyMaintenance * WEIGHT_SAFETY;

    // Clamp to valid range
    outcome = constrain(outcome, 0.0, 1.0);

    return outcome;
  }

  // ============================================
  // COMPONENT CALCULATIONS
  // ============================================

private:

  float calculateNeedImprovement(Behavior behavior, Needs& needsAfter) {
    float improvement = 0.0;

    // Stimulation need
    float stimChange = needsAfter.getStimulation() - needsSnapshot_stimulation;
    if (behavior == EXPLORE || behavior == INVESTIGATE || behavior == PLAY) {
      improvement += stimChange * 2.0;  // Weight for exploration behaviors
    } else {
      improvement += stimChange * 0.5;
    }

    // Social need
    float socialChange = needsAfter.getSocial() - needsSnapshot_social;
    if (behavior == SOCIAL_ENGAGE) {
      improvement += socialChange * 3.0;  // Strong weight for social behaviors
    } else {
      improvement += socialChange * 0.5;
    }

    // Energy need
    float energyChange = needsAfter.getEnergy() - needsSnapshot_energy;
    if (behavior == REST) {
      improvement += energyChange * 2.0;  // Weight for rest
    } else if (behavior == PLAY || behavior == EXPLORE) {
      improvement += energyChange * 0.3;  // Slight penalty for energetic behaviors
    }

    // Novelty need
    float noveltyChange = needsAfter.getNovelty() - needsSnapshot_novelty;
    if (behavior == INVESTIGATE) {
      improvement += -noveltyChange * 1.5;  // Investigating reduces novelty (good!)
    }

    return constrain(improvement, -0.3, 0.3);
  }

  float calculateEmotionalImprovement(Emotion& emotionAfter) {
    float improvement = 0.0;

    // Valence (positive emotion) improvement
    float valenceChange = emotionAfter.getValence() - emotionSnapshot_valence;
    improvement += valenceChange * 0.5;

    // Arousal optimization (prefer moderate arousal ~0.5)
    float arousalAfter = emotionAfter.getArousal();
    float arousalTarget = 0.5;

    float arousalImprovementBefore = abs(emotionSnapshot_arousal - arousalTarget);
    float arousalImprovementAfter = abs(arousalAfter - arousalTarget);
    float arousalChange = arousalImprovementBefore - arousalImprovementAfter;

    improvement += arousalChange * 0.2;

    return constrain(improvement, -0.2, 0.2);
  }

  float calculateGoalAlignment(Behavior behavior, GoalFormation* goalSystem) {
    GoalType goalType = goalSystem->getCurrentGoalType();

    // Check if behavior matches active goal
    bool aligned = false;

    switch(goalType) {
      case GOAL_INVESTIGATE_THOROUGHLY:
        aligned = (behavior == INVESTIGATE);
        break;
      case GOAL_SEEK_SOCIAL:
        aligned = (behavior == SOCIAL_ENGAGE);
        break;
      case GOAL_EXPLORE_AREA:
        aligned = (behavior == EXPLORE);
        break;
      case GOAL_UNDERSTAND_PATTERN:
        aligned = (behavior == INVESTIGATE || behavior == EXPLORE);
        break;
      case GOAL_EXPERIMENT:
        aligned = (behavior == PLAY);
        break;
      case GOAL_REST_FULLY:
        aligned = (behavior == REST);
        break;
      default:
        aligned = false;
    }

    return aligned ? 0.2 : 0.0;
  }

  float calculateSafetyMaintenance(Behavior behavior, Needs& needsAfter) {
    float safetyChange = needsAfter.getSafety() - needsSnapshot_safety;

    // Safety decrease is bad
    if (safetyChange < 0) {
      if (behavior == RETREAT) {
        return -0.15;  // Retreat didn't work - very bad!
      } else {
        return -0.05;  // Some safety loss
      }
    }

    // Safety increase is good
    if (safetyChange > 0) {
      return 0.05;
    }

    // No change is neutral
    return 0.0;
  }

public:

  // ============================================
  // DIAGNOSTICS
  // ============================================

  void printBreakdown(Behavior behavior, Needs& needsAfter,
                     Emotion& emotionAfter, GoalFormation* goalSystem) {

    Serial.println("\n[OUTCOME BREAKDOWN]");

    float needImp = calculateNeedImprovement(behavior, needsAfter);
    Serial.print("  Need improvement: ");
    Serial.print(needImp, 3);
    Serial.print(" × ");
    Serial.print(WEIGHT_NEEDS, 2);
    Serial.print(" = ");
    Serial.println(needImp * WEIGHT_NEEDS, 3);

    float emotionImp = calculateEmotionalImprovement(emotionAfter);
    Serial.print("  Emotion improvement: ");
    Serial.print(emotionImp, 3);
    Serial.print(" × ");
    Serial.print(WEIGHT_EMOTION, 2);
    Serial.print(" = ");
    Serial.println(emotionImp * WEIGHT_EMOTION, 3);

    if (goalSystem && goalSystem->hasActiveGoal()) {
      float goalAlign = calculateGoalAlignment(behavior, goalSystem);
      Serial.print("  Goal alignment: ");
      Serial.print(goalAlign, 3);
      Serial.print(" × ");
      Serial.print(WEIGHT_GOAL, 2);
      Serial.print(" = ");
      Serial.println(goalAlign * WEIGHT_GOAL, 3);
    }

    float safetyMaint = calculateSafetyMaintenance(behavior, needsAfter);
    Serial.print("  Safety maintenance: ");
    Serial.print(safetyMaint, 3);
    Serial.print(" × ");
    Serial.print(WEIGHT_SAFETY, 2);
    Serial.print(" = ");
    Serial.println(safetyMaint * WEIGHT_SAFETY, 3);

    float total = calculate(behavior, needsAfter, emotionAfter, goalSystem);
    Serial.print("  TOTAL OUTCOME: ");
    Serial.println(total, 3);
  }
};

#endif // OUTCOME_CALCULATOR_H
