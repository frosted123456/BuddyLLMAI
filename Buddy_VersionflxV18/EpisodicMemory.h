// EpisodicMemory.h
// Stores specific experiences as episodes: "I remember when..."
// Enables Buddy to recall past interactions and learn from specific events

#ifndef EPISODIC_MEMORY_H
#define EPISODIC_MEMORY_H

#include "Emotion.h"
#include "BehaviorSelection.h"

struct Episode {
  // Context
  unsigned long timestamp;      // When it happened (millis)
  Behavior behavior;            // What Buddy was doing
  EmotionLabel emotion;         // How Buddy felt
  
  // Situation
  float distance;               // Distance to object/person
  int direction;                // Which direction (0-7)
  bool humanPresent;            // Was a person there?
  
  // Outcome
  float outcome;                // How well did it go? (-1.0 to 1.0)
  bool wasSuccessful;           // Simple success flag
  
  // Salience (importance)
  float salience;               // How memorable (0.0-1.0)
  int recallCount;              // How many times recalled
  
  Episode() {
    timestamp = 0;
    behavior = IDLE;
    emotion = NEUTRAL;
    distance = 100.0;
    direction = 0;
    humanPresent = false;
    outcome = 0.0;
    wasSuccessful = false;
    salience = 0.0;
    recallCount = 0;
  }
};

class EpisodicMemory {
private:
  static const int MAX_EPISODES = 20;  // Store last 20 experiences
  Episode episodes[MAX_EPISODES];
  int currentIndex;
  int episodeCount;
  
  unsigned long lastRecallTime;
  int lastRecalledIndex;
  
public:
  EpisodicMemory() {
    currentIndex = 0;
    episodeCount = 0;
    lastRecallTime = 0;
    lastRecalledIndex = -1;
  }
  
  // ============================================
  // STORE NEW EPISODE
  // ============================================
  
  void recordEpisode(Behavior behavior, EmotionLabel emotion, 
                     float distance, int direction, bool humanPresent,
                     float outcome) {
    
    Episode& ep = episodes[currentIndex];
    
    ep.timestamp = millis();
    ep.behavior = behavior;
    ep.emotion = emotion;
    ep.distance = distance;
    ep.direction = direction;
    ep.humanPresent = humanPresent;
    ep.outcome = outcome;
    ep.wasSuccessful = (outcome > 0.5);
    ep.recallCount = 0;
    
    // Calculate salience (how memorable)
    ep.salience = calculateSalience(emotion, outcome, humanPresent);
    
    // Move to next slot (circular buffer)
    currentIndex = (currentIndex + 1) % MAX_EPISODES;
    if (episodeCount < MAX_EPISODES) {
      episodeCount++;
    }
    
    if (ep.salience > 0.7) {
      Serial.print("[EPISODIC] Memorable experience recorded (salience: ");
      Serial.print(ep.salience, 2);
      Serial.println(")");
    }
  }
  
  float calculateSalience(EmotionLabel emotion, float outcome, bool humanPresent) {
    float sal = 0.0;
    
    // Emotional intensity increases salience
    if (emotion == EXCITED || emotion == STARTLED || emotion == ANXIOUS) {
      sal += 0.4;
    } else if (emotion == CURIOUS || emotion == CONFUSED) {
      sal += 0.3;
    } else {
      sal += 0.1;
    }
    
    // Extreme outcomes (very good or very bad) are memorable
    sal += abs(outcome - 0.5) * 0.4;
    
    // Social interactions are more memorable
    if (humanPresent) {
      sal += 0.3;
    }
    
    return constrain(sal, 0.0, 1.0);
  }
  
  // ============================================
  // RECALL SIMILAR EPISODES
  // ============================================
  
  int recallSimilar(Behavior currentBehavior, int currentDirection, 
                    float currentDistance, Episode& recalled) {
    
    if (episodeCount == 0) return -1;
    
    int bestMatch = -1;
    float bestSimilarity = 0.0;
    
    for (int i = 0; i < episodeCount; i++) {
      Episode& ep = episodes[i];
      
      // Calculate similarity
      float similarity = 0.0;
      
      // Behavior match (strong weight)
      if (ep.behavior == currentBehavior) {
        similarity += 0.4;
      }
      
      // Direction match
      int dirDiff = abs(ep.direction - currentDirection);
      if (dirDiff > 4) dirDiff = 8 - dirDiff;  // Wrap around
      similarity += (1.0 - dirDiff / 4.0) * 0.2;
      
      // Distance match
      float distDiff = abs(ep.distance - currentDistance);
      similarity += (1.0 - constrain(distDiff / 100.0, 0.0, 1.0)) * 0.2;
      
      // Recency bonus (recent memories easier to recall)
      unsigned long age = millis() - ep.timestamp;
      float recencyBonus = constrain(1.0 - age / 300000.0, 0.0, 0.3);  // 5 min decay
      similarity += recencyBonus;
      
      // Salience boost (memorable events recalled easier)
      similarity += ep.salience * 0.2;
      
      if (similarity > bestSimilarity) {
        bestSimilarity = similarity;
        bestMatch = i;
      }
    }
    
    if (bestMatch >= 0 && bestSimilarity > 0.5) {
      recalled = episodes[bestMatch];
      episodes[bestMatch].recallCount++;
      lastRecalledIndex = bestMatch;
      lastRecallTime = millis();
      
      Serial.print("[EPISODIC] Recalled similar experience (similarity: ");
      Serial.print(bestSimilarity, 2);
      Serial.println(")");
      
      return bestMatch;
    }
    
    return -1;
  }
  
  // ============================================
  // RECALL BEST/WORST EPISODES
  // ============================================
  
  int recallBestExperience(Behavior behavior, Episode& recalled) {
    if (episodeCount == 0) return -1;
    
    int bestIndex = -1;
    float bestOutcome = -1.0;
    
    for (int i = 0; i < episodeCount; i++) {
      if (episodes[i].behavior == behavior && 
          episodes[i].outcome > bestOutcome) {
        bestOutcome = episodes[i].outcome;
        bestIndex = i;
      }
    }
    
    if (bestIndex >= 0) {
      recalled = episodes[bestIndex];
      episodes[bestIndex].recallCount++;
      
      Serial.print("[EPISODIC] Recalled best ");
      Serial.print(behaviorToString(behavior));
      Serial.print(" experience (outcome: ");
      Serial.print(bestOutcome, 2);
      Serial.println(")");
      
      return bestIndex;
    }
    
    return -1;
  }
  
  int recallWorstExperience(Behavior behavior, Episode& recalled) {
    if (episodeCount == 0) return -1;
    
    int worstIndex = -1;
    float worstOutcome = 2.0;  // Start high
    
    for (int i = 0; i < episodeCount; i++) {
      if (episodes[i].behavior == behavior && 
          episodes[i].outcome < worstOutcome) {
        worstOutcome = episodes[i].outcome;
        worstIndex = i;
      }
    }
    
    if (worstIndex >= 0) {
      recalled = episodes[worstIndex];
      episodes[worstIndex].recallCount++;
      
      Serial.print("[EPISODIC] Recalled worst ");
      Serial.print(behaviorToString(behavior));
      Serial.print(" experience (outcome: ");
      Serial.print(worstOutcome, 2);
      Serial.println(")");
      
      return worstIndex;
    }
    
    return -1;
  }
  
  // ============================================
  // RECALL EMOTIONAL EPISODES
  // ============================================
  
  int recallMostIntenseEmotion(Episode& recalled) {
    if (episodeCount == 0) return -1;
    
    int mostIntenseIndex = -1;
    float highestSalience = 0.0;
    
    for (int i = 0; i < episodeCount; i++) {
      if (episodes[i].salience > highestSalience) {
        highestSalience = episodes[i].salience;
        mostIntenseIndex = i;
      }
    }
    
    if (mostIntenseIndex >= 0) {
      recalled = episodes[mostIntenseIndex];
      episodes[mostIntenseIndex].recallCount++;
      
      Serial.print("[EPISODIC] Recalled intense ");
      Serial.print(emotionToString(recalled.emotion));
      Serial.print(" memory (salience: ");
      Serial.print(highestSalience, 2);
      Serial.println(")");
      
      return mostIntenseIndex;
    }
    
    return -1;
  }
  
  // ============================================
  // QUERY EPISODES
  // ============================================
  
  bool hasExperienceWith(Behavior behavior) {
    for (int i = 0; i < episodeCount; i++) {
      if (episodes[i].behavior == behavior) {
        return true;
      }
    }
    return false;
  }
  
  float getAverageOutcome(Behavior behavior) {
    float sum = 0.0;
    int count = 0;
    
    for (int i = 0; i < episodeCount; i++) {
      if (episodes[i].behavior == behavior) {
        sum += episodes[i].outcome;
        count++;
      }
    }
    
    return count > 0 ? sum / count : 0.5;
  }
  
  int countSuccessful(Behavior behavior) {
    int count = 0;
    for (int i = 0; i < episodeCount; i++) {
      if (episodes[i].behavior == behavior && episodes[i].wasSuccessful) {
        count++;
      }
    }
    return count;
  }
  
  int countSocialEpisodes() {
    int count = 0;
    for (int i = 0; i < episodeCount; i++) {
      if (episodes[i].humanPresent) {
        count++;
      }
    }
    return count;
  }
  
  // ============================================
  // MEMORY CONSOLIDATION
  // ============================================
  
  void consolidate() {
    unsigned long now = millis();
    
    for (int i = 0; i < episodeCount; i++) {
      // Calculate episode age
      unsigned long age = now - episodes[i].timestamp;
      float ageDays = age / (1000.0 * 60.0 * 60.0 * 24.0);
      
      // Ebbinghaus-style forgetting curve
      // Memory strength decreases with time unless recalled
      float ageDecay = 1.0 / (1.0 + 0.1 * ageDays);
      
      if (episodes[i].recallCount == 0) {
        // Not recalled recently: decay with age
        episodes[i].salience *= (0.95 * ageDecay);
      } else {
        // Recalled recently: strengthen (spaced repetition effect)
        episodes[i].salience *= 1.05;
        episodes[i].recallCount = 0;  // Reset for next consolidation period
      }
      
      // Ensure salience stays in valid range
      episodes[i].salience = constrain(episodes[i].salience, 0.0, 1.0);
    }
  }
  
  // ============================================
  // DIAGNOSTICS
  // ============================================
  
  void print() {
    Serial.println("--- EPISODIC MEMORY ---");
    Serial.print("  Episodes stored: ");
    Serial.print(episodeCount);
    Serial.print(" / ");
    Serial.println(MAX_EPISODES);
    
    if (episodeCount == 0) {
      Serial.println("  No experiences recorded yet");
      return;
    }
    
    Serial.println("\n  Recent memorable experiences:");
    
    // Show 5 most salient
    for (int shown = 0; shown < 5 && shown < episodeCount; shown++) {
      int mostSalient = -1;
      float highestSalience = -1.0;
      
      for (int i = 0; i < episodeCount; i++) {
        bool alreadyShown = false;
        for (int j = 0; j < shown; j++) {
          // Skip already shown (crude but works)
        }
        
        if (episodes[i].salience > highestSalience) {
          highestSalience = episodes[i].salience;
          mostSalient = i;
        }
      }
      
      if (mostSalient >= 0) {
        Episode& ep = episodes[mostSalient];
        unsigned long age = (millis() - ep.timestamp) / 1000;
        
        Serial.print("    [");
        Serial.print(age);
        Serial.print("s ago] ");
        Serial.print(behaviorToString(ep.behavior));
        Serial.print(" → ");
        Serial.print(emotionToString(ep.emotion));
        Serial.print(" (outcome:");
        Serial.print(ep.outcome, 1);
        Serial.print(" sal:");
        Serial.print(ep.salience, 2);
        Serial.println(")");
      }
    }
    
    Serial.print("\n  Social episodes: ");
    Serial.println(countSocialEpisodes());
    
    Serial.print("  Last recall: ");
    if (lastRecalledIndex >= 0) {
      Serial.print((millis() - lastRecallTime) / 1000);
      Serial.println("s ago");
    } else {
      Serial.println("never");
    }
  }
  
  void printCompact() {
    Serial.print("  [MEMORY] Episodes:");
    Serial.print(episodeCount);
    Serial.print(" Social:");
    Serial.println(countSocialEpisodes());
  }
  
  const char* behaviorToString(Behavior b) {
    switch(b) {
      case IDLE: return "IDLE";
      case EXPLORE: return "EXPLORE";
      case INVESTIGATE: return "INVESTIGATE";
      case SOCIAL_ENGAGE: return "SOCIAL";
      case RETREAT: return "RETREAT";
      case REST: return "REST";
      case PLAY: return "PLAY";
      case VIGILANT: return "VIGILANT";
      default: return "UNKNOWN";
    }
  }
  
  const char* emotionToString(EmotionLabel e) {
    switch(e) {
      case NEUTRAL: return "neutral";
      case EXCITED: return "excited";
      case CURIOUS: return "curious";
      case CONTENT: return "content";
      case ANXIOUS: return "anxious";
      case STARTLED: return "startled";
      case BORED: return "bored";
      case CONFUSED: return "confused";
      default: return "unknown";
    }
  }
};

#endif // EPISODIC_MEMORY_H