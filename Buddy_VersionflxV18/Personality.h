// Personality.h
// Stable personality traits with slow, bounded drift over time

#ifndef PERSONALITY_H
#define PERSONALITY_H

class Learning;  // Forward declaration

class Personality {
private:
  // Core traits (0.0 to 1.0)
  float curiosity;       // 0=incurious, 1=very curious
  float caution;         // 0=bold, 1=very cautious
  float sociability;     // 0=withdrawn, 1=outgoing
  float playfulness;     // 0=serious, 1=playful
  float excitability;    // 0=calm, 1=easily excited
  float persistence;     // 0=gives up easily, 1=very persistent
  float expressiveness;  // 0=subdued, 1=highly expressive
  
  // Drift bounds (prevent radical personality changes)
  const float MIN_TRAIT = 0.2;
  const float MAX_TRAIT = 0.8;
  
public:
  Personality() {
    // Start with balanced personality
    curiosity = 0.5;
    caution = 0.5;
    sociability = 0.5;
    playfulness = 0.5;
    excitability = 0.5;
    persistence = 0.5;
    expressiveness = 0.5;
  }
  
  // ============================================
  // PERSONALITY DRIFT (very slow adaptation)
  // ============================================
  
  void drift(Learning& learning, float driftRate);  // Defined after Learning class
  
  void adjustTrait(float& trait, float evidence, float driftRate) {
    if (abs(evidence) > 0.1) {  // Only drift with strong evidence
      float delta = driftRate * evidence;
      trait = constrain(trait + delta, MIN_TRAIT, MAX_TRAIT);
    }
  }
  
  // ============================================
  // DERIVED ATTRIBUTES
  // ============================================
  
  float getEffectiveCuriosity() {
    // Caution dampens curiosity
    return curiosity * (1.0 - caution * 0.4);
  }
  
  float getEffectiveSociability() {
    // Excitement amplifies social approach
    return sociability * (0.7 + excitability * 0.3);
  }
  
  float getExplorationStyle() {
    // How thorough explorations are
    return curiosity * persistence;
  }
  
  float getRiskTolerance() {
    return 1.0 - caution;
  }
  
  // ============================================
  // GETTERS
  // ============================================
  
  float getCuriosity() { return curiosity; }
  float getCaution() { return caution; }
  float getSociability() { return sociability; }
  float getPlayfulness() { return playfulness; }
  float getExcitability() { return excitability; }
  float getPersistence() { return persistence; }
  float getExpressiveness() { return expressiveness; }
  
  // ============================================
  // SETTERS (for loading from EEPROM)
  // ============================================
  
  void setCuriosity(float val) { curiosity = constrain(val, MIN_TRAIT, MAX_TRAIT); }
  void setCaution(float val) { caution = constrain(val, MIN_TRAIT, MAX_TRAIT); }
  void setSociability(float val) { sociability = constrain(val, MIN_TRAIT, MAX_TRAIT); }
  void setPlayfulness(float val) { playfulness = constrain(val, MIN_TRAIT, MAX_TRAIT); }
  void setExcitability(float val) { excitability = constrain(val, MIN_TRAIT, MAX_TRAIT); }
  void setPersistence(float val) { persistence = constrain(val, MIN_TRAIT, MAX_TRAIT); }
  void setExpressiveness(float val) { expressiveness = constrain(val, MIN_TRAIT, MAX_TRAIT); }
  
  // ============================================
  // DIAGNOSTICS
  // ============================================
  
  void print() {
    Serial.println("--- PERSONALITY ---");
    Serial.print("  Curiosity:       ");
    printBar(curiosity);
    Serial.println();
    Serial.print("  Caution:         ");
    printBar(caution);
    Serial.println();
    Serial.print("  Sociability:     ");
    printBar(sociability);
    Serial.println();
    Serial.print("  Playfulness:     ");
    printBar(playfulness);
    Serial.println();
    Serial.print("  Excitability:    ");
    printBar(excitability);
    Serial.println();
    Serial.print("  Persistence:     ");
    printBar(persistence);
    Serial.println();
    Serial.print("  Expressiveness:  ");
    printBar(expressiveness);
    Serial.println();
    
    Serial.println("\n  Derived Attributes:");
    Serial.print("    Effective Curiosity: ");
    Serial.println(getEffectiveCuriosity(), 2);
    Serial.print("    Risk Tolerance: ");
    Serial.println(getRiskTolerance(), 2);
    Serial.print("    Exploration Style: ");
    Serial.println(getExplorationStyle(), 2);
  }
  
  void printCompact() {
    Serial.print("  [PERSONALITY] C:");
    Serial.print(curiosity, 1);
    Serial.print(" Ca:");
    Serial.print(caution, 1);
    Serial.print(" S:");
    Serial.print(sociability, 1);
    Serial.print(" P:");
    Serial.print(playfulness, 1);
    Serial.println();
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
  
  // ============================================
  // PERSONALITY ARCHETYPES (for testing)
  // ============================================
  
  void setArchetype(int type) {
    switch(type) {
      case 1:  // Bold Explorer
        curiosity = 0.8;
        caution = 0.3;
        sociability = 0.6;
        playfulness = 0.7;
        excitability = 0.7;
        persistence = 0.6;
        expressiveness = 0.7;
        Serial.println("[PERSONALITY] Set to Bold Explorer");
        break;
        
      case 2:  // Shy Observer
        curiosity = 0.4;
        caution = 0.7;
        sociability = 0.3;
        playfulness = 0.3;
        excitability = 0.4;
        persistence = 0.7;
        expressiveness = 0.4;
        Serial.println("[PERSONALITY] Set to Shy Observer");
        break;
        
      case 3:  // Playful Friend
        curiosity = 0.6;
        caution = 0.4;
        sociability = 0.8;
        playfulness = 0.8;
        excitability = 0.7;
        persistence = 0.4;
        expressiveness = 0.8;
        Serial.println("[PERSONALITY] Set to Playful Friend");
        break;
        
      default:  // Balanced (default)
        curiosity = 0.5;
        caution = 0.5;
        sociability = 0.5;
        playfulness = 0.5;
        excitability = 0.5;
        persistence = 0.5;
        expressiveness = 0.5;
        Serial.println("[PERSONALITY] Set to Balanced");
        break;
    }
  }
};

#endif // PERSONALITY_H
