// AIBridge.h
// AI Integration Bridge for Python voice/vision assistant
// Handles serial commands prefixed with '!' to avoid conflicts with existing commands
// All responses are JSON terminated with newline
//
// Commands:
//   !QUERY              → Returns full state JSON (includes "animating" field)
//   !LOOK:base,nod      → Move servos (blocked during reflex tracking)
//   !SATISFY:need,amt   → Satisfy a need (social, stimulation, novelty)
//   !PRESENCE           → Simulate human presence detection
//   !EXPRESS:emotion     → Express an emotion (blocked during animation)
//   !NOD:count           → Nod yes animation
//   !SHAKE:count         → Shake no animation
//   !STREAM:on/off       → Toggle periodic state broadcast
//   !ATTENTION:dir       → Look in a direction (center/left/right/up/down)
//   !LISTENING           → Attentive pose for wake-word detection
//   !THINKING            → Looping pondering animation (non-blocking)
//   !STOP_THINKING       → Stop thinking animation
//   !SPEAKING            → Looping conversational micro-nods (non-blocking)
//   !STOP_SPEAKING       → Stop speaking animation
//   !ACKNOWLEDGE         → Quick subtle nod
//   !CELEBRATE           → Happy bounce animation
//   !IDLE                → Clear AI state, return to behavior system

#ifndef AI_BRIDGE_H
#define AI_BRIDGE_H

#include "BehaviorEngine.h"
#include "ServoController.h"
#include "AnimationController.h"
#include "ReflexiveControl.h"

// AI animation modes for non-blocking looping animations
enum AIAnimMode {
  AI_ANIM_NONE = 0,
  AI_ANIM_THINKING,
  AI_ANIM_SPEAKING
};

class AIBridge {
private:
  BehaviorEngine* engine;
  ServoController* servos;
  AnimationController* animator;
  ReflexiveControl* reflex;

  bool streamingEnabled;
  unsigned long lastStreamTime;
  static const unsigned long STREAM_INTERVAL = 500; // ms

  // Looping animation state
  AIAnimMode aiAnimMode;
  unsigned long aiAnimStartTime;
  unsigned long lastAiAnimStep;

public:
  AIBridge()
    : engine(nullptr), servos(nullptr), animator(nullptr), reflex(nullptr),
      streamingEnabled(false), lastStreamTime(0),
      aiAnimMode(AI_ANIM_NONE), aiAnimStartTime(0), lastAiAnimStep(0) {}

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
    // cmdLine is everything after '!' up to newline

    // Match longer prefixes first to avoid ambiguity
    if (strncmp(cmdLine, "STOP_THINKING", 13) == 0) {
      cmdStopThinking();
    }
    else if (strncmp(cmdLine, "STOP_SPEAKING", 13) == 0) {
      cmdStopSpeaking();
    }
    else if (strncmp(cmdLine, "ACKNOWLEDGE", 11) == 0) {
      cmdAcknowledge();
    }
    else if (strncmp(cmdLine, "ATTENTION:", 10) == 0) {
      cmdAttention(cmdLine + 10);
    }
    else if (strncmp(cmdLine, "LISTENING", 9) == 0) {
      cmdListening();
    }
    else if (strncmp(cmdLine, "CELEBRATE", 9) == 0) {
      cmdCelebrate();
    }
    else if (strncmp(cmdLine, "THINKING", 8) == 0) {
      cmdThinking();
    }
    else if (strncmp(cmdLine, "SPEAKING", 8) == 0) {
      cmdSpeaking();
    }
    else if (strncmp(cmdLine, "PRESENCE", 8) == 0) {
      cmdPresence();
    }
    else if (strncmp(cmdLine, "SATISFY:", 8) == 0) {
      cmdSatisfy(cmdLine + 8);
    }
    else if (strncmp(cmdLine, "EXPRESS:", 8) == 0) {
      cmdExpress(cmdLine + 8);
    }
    else if (strncmp(cmdLine, "STREAM:", 7) == 0) {
      cmdStream(cmdLine + 7);
    }
    else if (strncmp(cmdLine, "SHAKE:", 6) == 0) {
      cmdShake(cmdLine + 6);
    }
    else if (strncmp(cmdLine, "QUERY", 5) == 0) {
      cmdQuery();
    }
    else if (strncmp(cmdLine, "LOOK:", 5) == 0) {
      cmdLook(cmdLine + 5);
    }
    else if (strncmp(cmdLine, "NOD:", 4) == 0) {
      cmdNod(cmdLine + 4);
    }
    else if (strncmp(cmdLine, "IDLE", 4) == 0) {
      cmdIdle();
    }
    else {
      Serial.print("{\"ok\":false,\"reason\":\"unknown_command\",\"cmd\":\"");
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

  // ============================================
  // LOOPING ANIMATION UPDATE - call from loop()
  // Runs at 20Hz (50ms steps), fully non-blocking
  // ============================================

  void updateLoopingAnimation() {
    if (aiAnimMode == AI_ANIM_NONE) return;
    if (servos == nullptr) return;

    // Yield to reflex tracking - don't fight for servos
    if (reflex != nullptr && reflex->isActive()) return;

    unsigned long now = millis();

    // 20Hz animation rate
    if (now - lastAiAnimStep < 50) return;
    lastAiAnimStep = now;

    float elapsed = (now - aiAnimStartTime) / 1000.0f; // seconds

    if (aiAnimMode == AI_ANIM_THINKING) {
      doThinkingStep(elapsed);
    } else if (aiAnimMode == AI_ANIM_SPEAKING) {
      doSpeakingStep(elapsed);
    }
  }

  // True when a looping AI animation is running
  // Used by main loop to skip behavior engine servo commands
  bool isAIAnimating() { return aiAnimMode != AI_ANIM_NONE; }

private:

  // ============================================
  // Helper: clear any active AI animation mode
  // ============================================

  void stopAIAnim() {
    aiAnimMode = AI_ANIM_NONE;
  }

  // ============================================
  // Helper: check if servos are available for AI commands
  // Returns false and prints JSON error if blocked
  // ============================================

  bool checkServoAccess() {
    if (reflex != nullptr && reflex->isActive()) {
      Serial.println("{\"ok\":false,\"reason\":\"tracking_active\"}");
      return false;
    }
    if (servos == nullptr || engine == nullptr) {
      Serial.println("{\"ok\":false,\"reason\":\"not_initialized\"}");
      return false;
    }
    return true;
  }

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
    bool animating = (animator != nullptr && animator->isCurrentlyAnimating())
                     || (aiAnimMode != AI_ANIM_NONE);

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
    Serial.print(",\"animating\":");
    Serial.print(animating ? "true" : "false");
    Serial.print(",\"servoBase\":");
    Serial.print(base);
    Serial.print(",\"servoNod\":");
    Serial.print(nod);
    Serial.print(",\"servoTilt\":");
    Serial.print(tilt);

    // Consciousness state
    ConsciousnessLayer& consciousness = engine->getConsciousness();
    Serial.print(",\"epistemic\":\"");
    switch(consciousness.getEpistemicState()) {
        case EPIST_CONFIDENT: Serial.print("confident"); break;
        case EPIST_UNCERTAIN: Serial.print("uncertain"); break;
        case EPIST_CONFUSED: Serial.print("confused"); break;
        case EPIST_LEARNING: Serial.print("learning"); break;
        case EPIST_CONFLICTED: Serial.print("conflicted"); break;
        case EPIST_WONDERING: Serial.print("wondering"); break;
    }
    Serial.print("\"");
    Serial.print(",\"tension\":");
    Serial.print(consciousness.getTension(), 2);
    Serial.print(",\"wondering\":");
    Serial.print(consciousness.isWondering() ? "true" : "false");
    Serial.print(",\"selfAwareness\":");
    Serial.print(consciousness.getSelfAwareness(), 2);

    Serial.println("}");
  }

  // ============================================
  // !LOOK:base,nod - Move servos safely
  // ============================================

  void cmdLook(const char* args) {
    stopAIAnim();
    if (!checkServoAccess()) return;

    int base, nod;
    if (sscanf(args, "%d,%d", &base, &nod) != 2) {
      Serial.println("{\"ok\":false,\"reason\":\"parse_error\"}");
      return;
    }

    base = constrain(base, 10, 170);
    nod = constrain(nod, 80, 150);

    MovementStyleParams style = engine->getMovementStyle();

    int curBase, curNod, curTilt;
    servos->getPosition(curBase, curNod, curTilt);
    servos->smoothMoveTo(base, nod, curTilt, style);

    Serial.println("{\"ok\":true}");
  }

  // ============================================
  // !SATISFY:need,amount - Satisfy a homeostatic need
  // ============================================

  void cmdSatisfy(const char* args) {
    if (engine == nullptr) {
      Serial.println("{\"ok\":false,\"reason\":\"not_initialized\"}");
      return;
    }

    char needName[16];
    float amount = 0.0;

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
    stopAIAnim();

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
    stopAIAnim();

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
    stopAIAnim();

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
  // !ATTENTION:direction - Look in a direction
  // ============================================

  void cmdAttention(const char* args) {
    stopAIAnim();
    if (!checkServoAccess()) return;

    int base, nod;

    if (strcasecmp(args, "center") == 0)      { base = 90;  nod = 115; }
    else if (strcasecmp(args, "left") == 0)   { base = 140; nod = 115; }
    else if (strcasecmp(args, "right") == 0)  { base = 40;  nod = 115; }
    else if (strcasecmp(args, "up") == 0)     { base = 90;  nod = 90;  }
    else if (strcasecmp(args, "down") == 0)   { base = 90;  nod = 140; }
    else {
      Serial.print("{\"ok\":false,\"reason\":\"unknown_direction\",\"dir\":\"");
      Serial.print(args);
      Serial.println("\"}");
      return;
    }

    MovementStyleParams style = engine->getMovementStyle();

    int curBase, curNod, curTilt;
    servos->getPosition(curBase, curNod, curTilt);
    servos->smoothMoveTo(base, nod, curTilt, style);

    Serial.println("{\"ok\":true}");
  }

  // ============================================
  // !LISTENING - Attentive pose for wake-word
  // Quick move to centered, slightly raised head
  // ============================================

  void cmdListening() {
    stopAIAnim();
    if (!checkServoAccess()) return;

    // Attentive centered pose: head centered, slightly raised
    MovementStyleParams style = engine->getMovementStyle();
    style.speed = 0.7f;  // Quick but smooth

    int curBase, curNod, curTilt;
    servos->getPosition(curBase, curNod, curTilt);
    servos->smoothMoveTo(90, 105, curTilt, style);

    Serial.println("{\"ok\":true}");
  }

  // ============================================
  // !THINKING - Start looping pondering animation
  // Non-blocking: sets mode, updateLoopingAnimation() drives it
  // ============================================

  void cmdThinking() {
    stopAIAnim();

    if (servos == nullptr) {
      Serial.println("{\"ok\":false,\"reason\":\"not_initialized\"}");
      return;
    }

    // Don't start if reflex is actively tracking
    if (reflex != nullptr && reflex->isActive()) {
      Serial.println("{\"ok\":false,\"reason\":\"tracking_active\"}");
      return;
    }

    aiAnimMode = AI_ANIM_THINKING;
    aiAnimStartTime = millis();
    lastAiAnimStep = 0;

    Serial.println("{\"ok\":true}");
  }

  // ============================================
  // !STOP_THINKING - Stop thinking animation
  // ============================================

  void cmdStopThinking() {
    stopAIAnim();
    Serial.println("{\"ok\":true}");
  }

  // ============================================
  // !SPEAKING - Start looping conversational animation
  // Non-blocking: sets mode, updateLoopingAnimation() drives it
  // ============================================

  void cmdSpeaking() {
    stopAIAnim();

    if (servos == nullptr) {
      Serial.println("{\"ok\":false,\"reason\":\"not_initialized\"}");
      return;
    }

    if (reflex != nullptr && reflex->isActive()) {
      Serial.println("{\"ok\":false,\"reason\":\"tracking_active\"}");
      return;
    }

    aiAnimMode = AI_ANIM_SPEAKING;
    aiAnimStartTime = millis();
    lastAiAnimStep = 0;

    Serial.println("{\"ok\":true}");
  }

  // ============================================
  // !STOP_SPEAKING - Stop speaking animation
  // ============================================

  void cmdStopSpeaking() {
    stopAIAnim();
    Serial.println("{\"ok\":true}");
  }

  // ============================================
  // !ACKNOWLEDGE - Quick subtle nod
  // Brief blocking (~150ms) - acceptable for one-shot
  // ============================================

  void cmdAcknowledge() {
    if (servos == nullptr) {
      Serial.println("{\"ok\":false,\"reason\":\"not_initialized\"}");
      return;
    }

    if (reflex != nullptr && reflex->isActive()) {
      Serial.println("{\"ok\":false,\"reason\":\"tracking_active\"}");
      return;
    }

    int base, nod, tilt;
    servos->getPosition(base, nod, tilt);

    // Quick small nod: down 8 degrees, then back
    int nodDown = constrain(nod + 8, 80, 150);
    servos->directWrite(base, nodDown, false);
    delay(120);
    servos->directWrite(base, nod, false);

    Serial.println("{\"ok\":true}");
  }

  // ============================================
  // !CELEBRATE - Happy bounce animation
  // ============================================

  void cmdCelebrate() {
    stopAIAnim();

    if (animator == nullptr || engine == nullptr) {
      Serial.println("{\"ok\":false,\"reason\":\"not_initialized\"}");
      return;
    }

    if (reflex != nullptr && reflex->isActive()) {
      Serial.println("{\"ok\":false,\"reason\":\"tracking_active\"}");
      return;
    }

    Emotion& emo = engine->getEmotion();
    Personality& pers = engine->getPersonality();
    Needs& needs = engine->getNeeds();
    animator->playfulBounce(emo, pers, needs);

    Serial.println("{\"ok\":true}");
  }

  // ============================================
  // !IDLE - Clear AI state, return to behavior system
  // ============================================

  void cmdIdle() {
    stopAIAnim();

    if (servos != nullptr && engine != nullptr) {
      // Return to neutral position
      if (reflex == nullptr || !reflex->isActive()) {
        MovementStyleParams style = engine->getMovementStyle();
        int curBase, curNod, curTilt;
        servos->getPosition(curBase, curNod, curTilt);
        servos->smoothMoveTo(90, 115, curTilt, style);
      }
    }

    Serial.println("{\"ok\":true}");
  }

  // ============================================
  // LOOPING ANIMATION STEP FUNCTIONS
  // Called at 20Hz from updateLoopingAnimation()
  // All math is frame-based, no blocking calls
  // ============================================

  void doThinkingStep(float t) {
    // Pondering animation: slow scanning with curious tilt
    //
    // Base: gentle left-right sweep (6s period, 10 degree amplitude)
    // Nod:  subtle up-down drift   (8s period, 5 degree amplitude)
    //       centered at 108 (slightly raised = attentive)
    // Tilt: slow curious tilt       (7s period, 8 degree amplitude)

    float baseOffset = sin(t * 1.0472f) * 10.0f;  // 2*PI/6
    float nodOffset  = sin(t * 0.7854f) * 5.0f;   // 2*PI/8
    float tiltOffset = sin(t * 0.8976f) * 8.0f;   // 2*PI/7

    int targetBase = 90  + (int)baseOffset;
    int targetNod  = 108 + (int)nodOffset;
    int targetTilt = 90  + (int)tiltOffset;

    targetBase = constrain(targetBase, 10, 170);
    targetNod  = constrain(targetNod, 80, 150);
    targetTilt = constrain(targetTilt, 20, 150);

    servos->directWriteFull(targetBase, targetNod, targetTilt, false);
  }

  void doSpeakingStep(float t) {
    // Conversational animation: rhythmic nods with subtle drift
    //
    // Base: very slow drift        (10s period, 3 degree amplitude)
    // Nod:  gentle rhythmic nod    (1.5s period, 4 degree amplitude)
    //       centered at 112 (slightly forward = engaged)
    // Tilt: subtle variation        (5s period, 3 degree amplitude)

    float baseOffset = sin(t * 0.6283f) * 3.0f;   // 2*PI/10
    float nodOffset  = sin(t * 4.1888f) * 4.0f;   // 2*PI/1.5
    float tiltOffset = sin(t * 1.2566f) * 3.0f;   // 2*PI/5

    int targetBase = 90  + (int)baseOffset;
    int targetNod  = 112 + (int)nodOffset;
    int targetTilt = 85  + (int)tiltOffset;

    targetBase = constrain(targetBase, 10, 170);
    targetNod  = constrain(targetNod, 80, 150);
    targetTilt = constrain(targetTilt, 20, 150);

    servos->directWriteFull(targetBase, targetNod, targetTilt, false);
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
