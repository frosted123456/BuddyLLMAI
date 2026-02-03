// AIBridge.h
// AI Integration Bridge for Python voice/vision assistant
// Handles serial commands prefixed with '!' to avoid conflicts with existing commands
// All responses are JSON terminated with newline
//
// Commands:
//   !QUERY              → Returns full state JSON
//   !LOOK:base,nod      → Move servos (blocked during reflex tracking)
//   !SATISFY:need,amt   → Satisfy a need (social, stimulation, novelty)
//   !PRESENCE           → Simulate human presence detection
//   !EXPRESS:emotion     → Express an emotion (blocked during animation)
//   !NOD:count           → Nod yes animation
//   !SHAKE:count         → Shake no animation
//   !STREAM:on/off       → Toggle periodic state broadcast

#ifndef AI_BRIDGE_H
#define AI_BRIDGE_H

#include "BehaviorEngine.h"
#include "ServoController.h"
#include "AnimationController.h"
#include "ReflexiveControl.h"

class AIBridge {
private:
  BehaviorEngine* engine;
  ServoController* servos;
  AnimationController* animator;
  ReflexiveControl* reflex;

  bool streamingEnabled;
  unsigned long lastStreamTime;
  static const unsigned long STREAM_INTERVAL = 500; // ms

public:
  AIBridge()
    : engine(nullptr), servos(nullptr), animator(nullptr), reflex(nullptr),
      streamingEnabled(false), lastStreamTime(0) {}

  void init(BehaviorEngine* eng, ServoController* srv,
            AnimationController* anim, ReflexiveControl* ref) {
    engine = eng;
    servos = srv;
    animator = anim;
    reflex = ref;
  }

  // ============================================
  // MAIN COMMAND DISPATCHER
  // Called from serialEvent() after '!' is consumed
  // ============================================

  void handleCommand(const char* cmdLine) {
    // cmdLine is everything after '!' up to newline, e.g. "QUERY" or "LOOK:90,115"

    if (strncmp(cmdLine, "QUERY", 5) == 0) {
      cmdQuery();
    }
    else if (strncmp(cmdLine, "LOOK:", 5) == 0) {
      cmdLook(cmdLine + 5);
    }
    else if (strncmp(cmdLine, "SATISFY:", 8) == 0) {
      cmdSatisfy(cmdLine + 8);
    }
    else if (strncmp(cmdLine, "PRESENCE", 8) == 0) {
      cmdPresence();
    }
    else if (strncmp(cmdLine, "EXPRESS:", 8) == 0) {
      cmdExpress(cmdLine + 8);
    }
    else if (strncmp(cmdLine, "NOD:", 4) == 0) {
      cmdNod(cmdLine + 4);
    }
    else if (strncmp(cmdLine, "SHAKE:", 6) == 0) {
      cmdShake(cmdLine + 6);
    }
    else if (strncmp(cmdLine, "STREAM:", 7) == 0) {
      cmdStream(cmdLine + 7);
    }
    else {
      Serial.print("{\"ok\":false,\"reason\":\"unknown_command\",\"cmd\":\"");
      // Print up to 20 chars of the command for debugging
      for (int i = 0; i < 20 && cmdLine[i] != '\0'; i++) {
        char c = cmdLine[i];
        if (c == '"' || c == '\\') Serial.print('\\');
        Serial.print(c);
      }
      Serial.println("\"}");
    }
  }

  // ============================================
  // STREAMING UPDATE - call from loop()
  // ============================================

  void updateStreaming() {
    if (!streamingEnabled) return;
    unsigned long now = millis();
    if (now - lastStreamTime >= STREAM_INTERVAL) {
      lastStreamTime = now;
      Serial.print("STATE:");
      sendStateJSON();
    }
  }

  bool isStreaming() { return streamingEnabled; }

private:

  // ============================================
  // !QUERY - Return full state as JSON
  // ============================================

  void cmdQuery() {
    sendStateJSON();
  }

  void sendStateJSON() {
    if (engine == nullptr || servos == nullptr) {
      Serial.println("{\"ok\":false,\"reason\":\"not_initialized\"}");
      return;
    }

    Emotion& emo = engine->getEmotion();
    Needs& needs = engine->getNeeds();
    Behavior beh = engine->getCurrentBehavior();

    int base, nod, tilt;
    servos->getPosition(base, nod, tilt);

    bool tracking = (reflex != nullptr && reflex->isActive());

    // Build JSON manually to avoid dynamic allocation
    Serial.print("{\"arousal\":");
    Serial.print(emo.getArousal(), 2);
    Serial.print(",\"valence\":");
    Serial.print(emo.getValence(), 2);
    Serial.print(",\"dominance\":");
    Serial.print(emo.getDominance(), 2);
    Serial.print(",\"emotion\":\"");
    Serial.print(emo.getLabelString());
    Serial.print("\",\"behavior\":\"");
    Serial.print(behaviorName(beh));
    Serial.print("\",\"stimulation\":");
    Serial.print(needs.getStimulation(), 2);
    Serial.print(",\"social\":");
    Serial.print(needs.getSocial(), 2);
    Serial.print(",\"energy\":");
    Serial.print(needs.getEnergy(), 2);
    Serial.print(",\"safety\":");
    Serial.print(needs.getSafety(), 2);
    Serial.print(",\"novelty\":");
    Serial.print(needs.getNovelty(), 2);
    Serial.print(",\"tracking\":");
    Serial.print(tracking ? "true" : "false");
    Serial.print(",\"servoBase\":");
    Serial.print(base);
    Serial.print(",\"servoNod\":");
    Serial.print(nod);
    Serial.print(",\"servoTilt\":");
    Serial.print(tilt);
    Serial.println("}");
  }

  // ============================================
  // !LOOK:base,nod - Move servos safely
  // ============================================

  void cmdLook(const char* args) {
    if (reflex != nullptr && reflex->isActive()) {
      Serial.println("{\"ok\":false,\"reason\":\"tracking_active\"}");
      return;
    }

    int base, nod;
    if (sscanf(args, "%d,%d", &base, &nod) != 2) {
      Serial.println("{\"ok\":false,\"reason\":\"parse_error\"}");
      return;
    }

    // Clamp to safe ranges
    base = constrain(base, 10, 170);
    nod = constrain(nod, 80, 150);

    if (servos != nullptr) {
      MovementStyleParams style;
      if (engine != nullptr) {
        style = engine->getMovementStyle();
      } else {
        // Fallback defaults
        style.speed = 0.5;
        style.smoothness = 0.8;
        style.amplitude = 1.0;
        style.directness = 0.8;
        style.hesitation = 0.0;
      }

      int currentBase, currentNod, currentTilt;
      servos->getPosition(currentBase, currentNod, currentTilt);
      servos->smoothMoveTo(base, nod, currentTilt, style);

      Serial.println("{\"ok\":true}");
    } else {
      Serial.println("{\"ok\":false,\"reason\":\"no_servos\"}");
    }
  }

  // ============================================
  // !SATISFY:need,amount - Satisfy a homeostatic need
  // ============================================

  void cmdSatisfy(const char* args) {
    if (engine == nullptr) {
      Serial.println("{\"ok\":false,\"reason\":\"not_initialized\"}");
      return;
    }

    // Parse need name and amount
    char needName[16];
    float amount = 0.0;

    // Manual parse: find comma
    const char* comma = strchr(args, ',');
    if (comma == nullptr) {
      Serial.println("{\"ok\":false,\"reason\":\"parse_error\"}");
      return;
    }

    int nameLen = comma - args;
    if (nameLen <= 0 || nameLen >= (int)sizeof(needName)) {
      Serial.println("{\"ok\":false,\"reason\":\"parse_error\"}");
      return;
    }

    strncpy(needName, args, nameLen);
    needName[nameLen] = '\0';
    amount = atof(comma + 1);

    // Clamp amount
    if (amount < 0.0f) amount = 0.0f;
    if (amount > 1.0f) amount = 1.0f;

    Needs& needs = engine->getNeeds();
    float resultValue = 0.0;

    if (strcmp(needName, "social") == 0) {
      needs.satisfySocial(amount);
      resultValue = needs.getSocial();
    }
    else if (strcmp(needName, "stimulation") == 0) {
      needs.satisfyStimulation(amount);
      resultValue = needs.getStimulation();
    }
    else if (strcmp(needName, "novelty") == 0) {
      needs.satisfyNovelty(amount);
      resultValue = needs.getNovelty();
    }
    else {
      Serial.print("{\"ok\":false,\"reason\":\"unknown_need\",\"need\":\"");
      Serial.print(needName);
      Serial.println("\"}");
      return;
    }

    Serial.print("{\"ok\":true,\"need\":\"");
    Serial.print(needName);
    Serial.print("\",\"value\":");
    Serial.print(resultValue, 2);
    Serial.println("}");
  }

  // ============================================
  // !PRESENCE - Simulate human presence detection
  // ============================================

  void cmdPresence() {
    if (engine == nullptr) {
      Serial.println("{\"ok\":false,\"reason\":\"not_initialized\"}");
      return;
    }

    Needs& needs = engine->getNeeds();
    needs.detectHumanPresence();

    Serial.println("{\"ok\":true}");
  }

  // ============================================
  // !EXPRESS:emotion - Express an emotion via animation
  // ============================================

  void cmdExpress(const char* args) {
    if (animator == nullptr || engine == nullptr) {
      Serial.println("{\"ok\":false,\"reason\":\"not_initialized\"}");
      return;
    }

    if (animator->isCurrentlyAnimating()) {
      Serial.println("{\"ok\":false,\"reason\":\"animating\"}");
      return;
    }

    if (reflex != nullptr && reflex->isActive()) {
      Serial.println("{\"ok\":false,\"reason\":\"tracking_active\"}");
      return;
    }

    EmotionLabel label;
    if (!parseEmotionLabel(args, label)) {
      Serial.print("{\"ok\":false,\"reason\":\"unknown_emotion\",\"emotion\":\"");
      Serial.print(args);
      Serial.println("\"}");
      return;
    }

    Personality& pers = engine->getPersonality();
    Needs& needs = engine->getNeeds();
    animator->expressEmotion(label, pers, needs);

    Serial.println("{\"ok\":true}");
  }

  // ============================================
  // !NOD:count - Nod yes animation
  // ============================================

  void cmdNod(const char* args) {
    if (animator == nullptr || engine == nullptr) {
      Serial.println("{\"ok\":false,\"reason\":\"not_initialized\"}");
      return;
    }

    if (animator->isCurrentlyAnimating()) {
      Serial.println("{\"ok\":false,\"reason\":\"animating\"}");
      return;
    }

    if (reflex != nullptr && reflex->isActive()) {
      Serial.println("{\"ok\":false,\"reason\":\"tracking_active\"}");
      return;
    }

    int count = atoi(args);
    if (count < 1) count = 1;
    if (count > 10) count = 10;

    Emotion& emo = engine->getEmotion();
    Personality& pers = engine->getPersonality();
    Needs& needs = engine->getNeeds();
    animator->nodYes(count, emo, pers, needs);

    Serial.println("{\"ok\":true}");
  }

  // ============================================
  // !SHAKE:count - Shake no animation
  // ============================================

  void cmdShake(const char* args) {
    if (animator == nullptr || engine == nullptr) {
      Serial.println("{\"ok\":false,\"reason\":\"not_initialized\"}");
      return;
    }

    if (animator->isCurrentlyAnimating()) {
      Serial.println("{\"ok\":false,\"reason\":\"animating\"}");
      return;
    }

    if (reflex != nullptr && reflex->isActive()) {
      Serial.println("{\"ok\":false,\"reason\":\"tracking_active\"}");
      return;
    }

    int count = atoi(args);
    if (count < 1) count = 1;
    if (count > 10) count = 10;

    Emotion& emo = engine->getEmotion();
    Personality& pers = engine->getPersonality();
    Needs& needs = engine->getNeeds();
    animator->shakeNo(count, emo, pers, needs);

    Serial.println("{\"ok\":true}");
  }

  // ============================================
  // !STREAM:on/off - Toggle state streaming
  // ============================================

  void cmdStream(const char* args) {
    if (strcmp(args, "on") == 0) {
      streamingEnabled = true;
      lastStreamTime = millis();
      Serial.println("{\"ok\":true,\"streaming\":true}");
    }
    else if (strcmp(args, "off") == 0) {
      streamingEnabled = false;
      Serial.println("{\"ok\":true,\"streaming\":false}");
    }
    else {
      Serial.println("{\"ok\":false,\"reason\":\"use_on_or_off\"}");
    }
  }

  // ============================================
  // HELPERS
  // ============================================

  bool parseEmotionLabel(const char* str, EmotionLabel& out) {
    if (strcasecmp(str, "curious") == 0)   { out = CURIOUS;   return true; }
    if (strcasecmp(str, "excited") == 0)   { out = EXCITED;   return true; }
    if (strcasecmp(str, "content") == 0)   { out = CONTENT;   return true; }
    if (strcasecmp(str, "anxious") == 0)   { out = ANXIOUS;   return true; }
    if (strcasecmp(str, "neutral") == 0)   { out = NEUTRAL;   return true; }
    if (strcasecmp(str, "startled") == 0)  { out = STARTLED;  return true; }
    if (strcasecmp(str, "bored") == 0)     { out = BORED;     return true; }
    if (strcasecmp(str, "confused") == 0)  { out = CONFUSED;  return true; }
    return false;
  }

  const char* behaviorName(Behavior b) {
    switch (b) {
      case IDLE:           return "IDLE";
      case EXPLORE:        return "EXPLORE";
      case INVESTIGATE:    return "INVESTIGATE";
      case SOCIAL_ENGAGE:  return "SOCIAL_ENGAGE";
      case RETREAT:        return "RETREAT";
      case REST:           return "REST";
      case PLAY:           return "PLAY";
      case VIGILANT:       return "VIGILANT";
      default:             return "UNKNOWN";
    }
  }
};

#endif // AI_BRIDGE_H
