// GoalFormation.h
// Multi-step goal setting and pursuit: "I want to..."
// Makes Buddy pursue intentions across multiple behavior cycles

#ifndef GOAL_FORMATION_H
#define GOAL_FORMATION_H

#include "BehaviorSelection.h"
#include "Emotion.h"
#include "Personality.h"

// Forward declaration for memory integration
class EpisodicMemory;

enum GoalType {
  GOAL_NONE,
  GOAL_INVESTIGATE_THOROUGHLY,  // "Study this thing carefully"
  GOAL_SEEK_SOCIAL,              // "Find someone to interact with"
  GOAL_EXPLORE_AREA,             // "Check out this direction"
  GOAL_UNDERSTAND_PATTERN,       // "Figure out what's happening"
  GOAL_EXPERIMENT,               // "Try something new"
  GOAL_REST_FULLY                // "Recover energy completely"
};

struct Goal {
  GoalType type;
  int targetDirection;           // Where to focus
  float targetDistance;          // How far
  
  float urgency;                 // How important (0.0-1.0)
  float progress;                // How far along (0.0-1.0)
  unsigned long startTime;       // When goal was formed
  unsigned long lastUpdate;      // Last progress update
  
  int stepsCompleted;            // How many sub-steps done
  int stepsRequired;             // Total sub-steps needed
  
  bool isActive;
  bool isComplete;
  bool wasAbandoned;
  
  Goal() {
    type = GOAL_NONE;
    targetDirection = 0;
    targetDistance = 50.0;
    urgency = 0.0;
    progress = 0.0;
    startTime = 0;
    lastUpdate = 0;
    stepsCompleted = 0;
    stepsRequired = 0;
    isActive = false;
    isComplete = false;
    wasAbandoned = false;
  }
};

class GoalFormation {
private:
  Goal currentGoal;
  Goal previousGoal;
  
  EpisodicMemory* episodicMemory;  // PACKAGE 2: Reference to memory system
  
  unsigned long lastGoalFormation;
  float goalFormationThreshold;
  
  int consecutiveFailures;
  
  // PACKAGE 2: Helper to map goal type to primary behavior
  Behavior getBehaviorForGoalType(GoalType type) {
    switch(type) {
      case GOAL_INVESTIGATE_THOROUGHLY: return INVESTIGATE;
      case GOAL_SEEK_SOCIAL: return SOCIAL_ENGAGE;
      case GOAL_EXPLORE_AREA: return EXPLORE;
      case GOAL_UNDERSTAND_PATTERN: return INVESTIGATE;
      case GOAL_EXPERIMENT: return PLAY;
      case GOAL_REST_FULLY: return REST;
      default: return IDLE;
    }
  }
  
public:
  GoalFormation() {
    lastGoalFormation = 0;
    goalFormationThreshold = 0.6;
    consecutiveFailures = 0;
    episodicMemory = nullptr;  // PACKAGE 2
  }
  
  // PACKAGE 2: Set memory reference
  void setEpisodicMemory(EpisodicMemory* mem) {
    episodicMemory = mem;
  }
  
  // ============================================
  // GOAL FORMATION (emergence of intention)
  // ============================================
  
  bool shouldFormGoal(Behavior currentBehavior, Emotion& emotion, 
                      Personality& personality, float curiosityLevel,
                      float socialNeed) {
    
    // Don't form goals too frequently
    if (millis() - lastGoalFormation < 10000) {  // 10 second cooldown
      return false;
    }
    
    // Don't form goals during retreat or rest
    if (currentBehavior == RETREAT || currentBehavior == REST) {
      return false;
    }
    
    // Higher chance if curious and aroused
    float formationChance = 0.0;
    formationChance += personality.getCuriosity() * 0.3;
    formationChance += emotion.getArousal() * 0.2;
    formationChance += personality.getPersistence() * 0.2;
    
    // Strong needs increase goal formation
    if (curiosityLevel > 0.7) formationChance += 0.2;
    if (socialNeed > 0.7) formationChance += 0.2;
    
    return formationChance > goalFormationThreshold;
  }
  
  void formGoal(GoalType type, int direction, float distance, 
                Personality& personality, Emotion& emotion) {
    
    // Save previous goal if active
    if (currentGoal.isActive && !currentGoal.isComplete) {
      previousGoal = currentGoal;
      previousGoal.wasAbandoned = true;
      
      Serial.println("[GOAL] Abandoning previous goal for new intention");
    }
    
    currentGoal = Goal();  // Reset
    currentGoal.type = type;
    currentGoal.targetDirection = direction;
    currentGoal.targetDistance = distance;
    currentGoal.startTime = millis();
    currentGoal.lastUpdate = millis();
    currentGoal.isActive = true;
    
    // Set requirements based on goal type
    switch(type) {
      case GOAL_INVESTIGATE_THOROUGHLY:
        currentGoal.stepsRequired = 3;
        currentGoal.urgency = 0.7;
        Serial.println("[GOAL FORMED] Investigate thoroughly");
        break;
        
      case GOAL_SEEK_SOCIAL:
        currentGoal.stepsRequired = 4;
        currentGoal.urgency = 0.8;
        Serial.println("[GOAL FORMED] Seek social interaction");
        break;
        
      case GOAL_EXPLORE_AREA:
        currentGoal.stepsRequired = 5;
        currentGoal.urgency = 0.6;
        Serial.println("[GOAL FORMED] Explore area");
        break;
        
      case GOAL_UNDERSTAND_PATTERN:
        currentGoal.stepsRequired = 6;
        currentGoal.urgency = 0.7;
        Serial.println("[GOAL FORMED] Understand pattern");
        break;
        
      case GOAL_EXPERIMENT:
        currentGoal.stepsRequired = 3;
        currentGoal.urgency = 0.5;
        Serial.println("[GOAL FORMED] Experiment");
        break;
        
      case GOAL_REST_FULLY:
        currentGoal.stepsRequired = 2;
        currentGoal.urgency = 0.9;
        Serial.println("[GOAL FORMED] Rest fully");
        break;
        
      default:
        currentGoal.stepsRequired = 3;
        currentGoal.urgency = 0.5;
        break;
    }
    
    // Personality modulates requirements
    if (personality.getPersistence() > 0.7) {
      currentGoal.stepsRequired += 1;  // More thorough
    }
    
    lastGoalFormation = millis();
    consecutiveFailures = 0;
    
    Serial.print("  Target: dir ");
    Serial.print(direction);
    Serial.print(", dist ");
    Serial.print(distance, 0);
    Serial.print("cm, steps ");
    Serial.println(currentGoal.stepsRequired);
  }
  
  // ============================================
  // GOAL PURSUIT (multi-step execution)
  // ============================================
  
  Behavior pursueSuggestedBehavior(Behavior originalChoice, 
                                    Personality& personality) {
    
    if (!currentGoal.isActive || currentGoal.isComplete) {
      return originalChoice;  // No active goal, use original
    }
    
    // Check if pursuing goal for too long
    unsigned long goalAge = millis() - currentGoal.startTime;
    if (goalAge > 60000) {  // 1 minute max
      Serial.println("[GOAL] Timeout - abandoning goal");
      abandonGoal();
      return originalChoice;
    }
    
    // Suggest behavior to advance goal
    Behavior suggested = originalChoice;
    
    switch(currentGoal.type) {
      case GOAL_INVESTIGATE_THOROUGHLY:
        suggested = INVESTIGATE;
        break;
        
      case GOAL_SEEK_SOCIAL:
        suggested = SOCIAL_ENGAGE;
        break;
        
      case GOAL_EXPLORE_AREA:
        suggested = EXPLORE;
        break;
        
      case GOAL_UNDERSTAND_PATTERN:
        // Alternate between investigate and explore
        suggested = (currentGoal.stepsCompleted % 2 == 0) ? INVESTIGATE : EXPLORE;
        break;
        
      case GOAL_EXPERIMENT:
        suggested = PLAY;
        break;
        
      case GOAL_REST_FULLY:
        suggested = REST;
        break;
        
      default:
        suggested = originalChoice;
        break;
    }
    
    // With low persistence, might abandon goal
    if (personality.getPersistence() < 0.4 && random(100) < 30) {
      Serial.println("[GOAL] Low persistence - considering abandonment");
      return originalChoice;  // Don't force it
    }
    
    return suggested;
  }
  
  void recordProgress(Behavior executedBehavior, float outcome) {
    if (!currentGoal.isActive || currentGoal.isComplete) return;
    
    currentGoal.lastUpdate = millis();
    
    // Check if behavior matches goal
    bool advancedGoal = false;
    
    switch(currentGoal.type) {
      case GOAL_INVESTIGATE_THOROUGHLY:
        if (executedBehavior == INVESTIGATE) advancedGoal = true;
        break;
        
      case GOAL_SEEK_SOCIAL:
        if (executedBehavior == SOCIAL_ENGAGE) advancedGoal = true;
        break;
        
      case GOAL_EXPLORE_AREA:
        if (executedBehavior == EXPLORE) advancedGoal = true;
        break;
        
      case GOAL_UNDERSTAND_PATTERN:
        if (executedBehavior == INVESTIGATE || executedBehavior == EXPLORE) {
          advancedGoal = true;
        }
        break;
        
      case GOAL_EXPERIMENT:
        if (executedBehavior == PLAY) advancedGoal = true;
        break;
        
      case GOAL_REST_FULLY:
        if (executedBehavior == REST) advancedGoal = true;
        break;
    }
    
    if (advancedGoal) {
      // Good outcome = more progress
      if (outcome > 0.5) {
        currentGoal.stepsCompleted++;
        currentGoal.progress = (float)currentGoal.stepsCompleted / currentGoal.stepsRequired;
        
        Serial.print("[GOAL PROGRESS] Step ");
        Serial.print(currentGoal.stepsCompleted);
        Serial.print("/");
        Serial.print(currentGoal.stepsRequired);
        Serial.print(" (");
        Serial.print(currentGoal.progress * 100, 0);
        Serial.println("%)");
        
        consecutiveFailures = 0;
        
        // Check completion
        if (currentGoal.stepsCompleted >= currentGoal.stepsRequired) {
          completeGoal();
        }
      } else {
        // Poor outcome
        consecutiveFailures++;
        Serial.print("[GOAL] Poor outcome (failures: ");
        Serial.print(consecutiveFailures);
        Serial.println(")");
        
        if (consecutiveFailures >= 3) {
          Serial.println("[GOAL] Too many failures - abandoning");
          abandonGoal();
        }
      }
    }
  }
  
  void completeGoal() {
    currentGoal.isComplete = true;
    currentGoal.isActive = false;
    currentGoal.progress = 1.0;
    
    unsigned long duration = (millis() - currentGoal.startTime) / 1000;
    
    Serial.println("\n[GOAL COMPLETE] ✓");
    Serial.print("  Type: ");
    Serial.println(goalTypeToString(currentGoal.type));
    Serial.print("  Duration: ");
    Serial.print(duration);
    Serial.println(" seconds");
    Serial.print("  Steps: ");
    Serial.print(currentGoal.stepsCompleted);
    Serial.println("\n");
    
    // PACKAGE 2: Record as memorable episode
    if (episodicMemory != nullptr) {
      // Goal completion is a highly salient positive experience
      Behavior finalBehavior = getBehaviorForGoalType(currentGoal.type);
      
      episodicMemory->recordEpisode(
        finalBehavior,
        EXCITED,  // Emotional state on completion
        currentGoal.targetDistance,
        currentGoal.targetDirection,
        false,  // humanPresent (unknown at this level)
        1.0     // Excellent outcome (goal completed!)
      );
      
      Serial.println("[MEMORY] Goal achievement recorded as memorable episode");
    }
  }
  
  void abandonGoal() {
    currentGoal.wasAbandoned = true;
    currentGoal.isActive = false;
    
    Serial.println("[GOAL] Abandoned (new priorities emerged)");
    
    // PACKAGE 2: Record as low-salience negative episode
    if (episodicMemory != nullptr) {
      Behavior finalBehavior = getBehaviorForGoalType(currentGoal.type);
      
      episodicMemory->recordEpisode(
        finalBehavior,
        CONFUSED,  // Emotional state on abandonment
        currentGoal.targetDistance,
        currentGoal.targetDirection,
        false,
        0.3     // Poor outcome (abandoned)
      );
      
      Serial.println("[MEMORY] Goal abandonment recorded");
    }
    
    // Reset failure counter
    consecutiveFailures = 0;
  }
  
  // ============================================
  // GOAL INTERRUPTION & RESUMPTION
  // ============================================
  
  bool canInterruptGoal(float urgency) {
    if (!currentGoal.isActive) return true;
    
    // High urgency can interrupt
    if (urgency > currentGoal.urgency + 0.3) {
      Serial.println("[GOAL] Interrupted by urgent need");
      return true;
    }
    
    return false;
  }
  
  bool shouldResumeGoal() {
    // Can resume if goal was interrupted (not abandoned or completed)
    return currentGoal.isActive && 
           !currentGoal.isComplete && 
           !currentGoal.wasAbandoned &&
           (millis() - currentGoal.lastUpdate) < 30000;  // 30 sec max interruption
  }
  
  // ============================================
  // GETTERS
  // ============================================
  
  bool hasActiveGoal() { return currentGoal.isActive && !currentGoal.isComplete; }
  GoalType getCurrentGoalType() { return currentGoal.type; }
  float getGoalProgress() { return currentGoal.progress; }
  float getGoalUrgency() { return currentGoal.urgency; }
  int getTargetDirection() { return currentGoal.targetDirection; }
  
  // ============================================
  // DIAGNOSTICS
  // ============================================
  
  void print() {
    Serial.println("--- GOAL FORMATION ---");
    
    if (currentGoal.isActive) {
      Serial.println("  ACTIVE GOAL:");
      Serial.print("    Type: ");
      Serial.println(goalTypeToString(currentGoal.type));
      Serial.print("    Progress: ");
      Serial.print(currentGoal.progress * 100, 0);
      Serial.print("% (");
      Serial.print(currentGoal.stepsCompleted);
      Serial.print("/");
      Serial.print(currentGoal.stepsRequired);
      Serial.println(")");
      Serial.print("    Urgency: ");
      Serial.println(currentGoal.urgency, 2);
      Serial.print("    Age: ");
      Serial.print((millis() - currentGoal.startTime) / 1000);
      Serial.println(" seconds");
    } else {
      Serial.println("  No active goal");
    }
    
    if (previousGoal.wasAbandoned) {
      Serial.println("\n  Previous goal: ABANDONED");
      Serial.print("    Was: ");
      Serial.println(goalTypeToString(previousGoal.type));
    }
  }
  
  void printCompact() {
    if (currentGoal.isActive) {
      Serial.print("  [GOAL] ");
      Serial.print(goalTypeToString(currentGoal.type));
      Serial.print(" (");
      Serial.print(currentGoal.progress * 100, 0);
      Serial.println("%)");
    }
  }
  
  const char* goalTypeToString(GoalType g) {
    switch(g) {
      case GOAL_NONE: return "none";
      case GOAL_INVESTIGATE_THOROUGHLY: return "investigate thoroughly";
      case GOAL_SEEK_SOCIAL: return "seek social";
      case GOAL_EXPLORE_AREA: return "explore area";
      case GOAL_UNDERSTAND_PATTERN: return "understand pattern";
      case GOAL_EXPERIMENT: return "experiment";
      case GOAL_REST_FULLY: return "rest fully";
      default: return "unknown";
    }
  }
};

#endif // GOAL_FORMATION_H