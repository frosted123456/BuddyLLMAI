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
//   !SPOKE               → Acknowledge spontaneous speech (resets urge)
//   !VISION:json         → Update behavior engine with PC vision observations (Phase 2)

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

  // Response stream routing (Phase 1A: BUG-1 fix)
  // Commands arriving via ESP32 WiFi bridge (Serial1) need responses
  // routed back to Serial1, not USB Serial.
  Stream* responseStream;

public:
  AIBridge()
    : engine(nullptr), servos(nullptr), animator(nullptr), reflex(nullptr),
      streamingEnabled(false), lastStreamTime(0),
      aiAnimMode(AI_ANIM_NONE), aiAnimStartTime(0), lastAiAnimStep(0),
      responseStream(&Serial) {}

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

  // Overload: route responses to a specific stream (e.g. Serial1 for ESP32 bridge)
  void handleCommand(const char* cmdLine, Stream* respondTo) {
    if (respondTo != nullptr) {
      responseStream = respondTo;
    } else {
      responseStream = &Serial;
    }
    handleCommand(cmdLine);
  }

  void handleCommand(const char* cmdLine) {
    // cmdLine is everything after '!' up to newline
    // Responses go to responseStream (default: USB Serial, or Serial1 if routed)

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
    // Phase 2: Vision feedback command — closes the autonomous observation loop
    else if (strncmp(cmdLine, "VISION:", 7) == 0) {
      cmdVision(cmdLine + 7);
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
    else if (strncmp(cmdLine, "SPOKE", 5) == 0) {
      cmdSpoke();
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
      responseStream->print("{\"ok\":false,\"reason\":\"unknown_command\",\"cmd\":\"");
      for (int i = 0; i < 20 && cmdLine[i] != '\0'; i++) {
        char c = cmdLine[i];
        if (c == '"' || c == '\\') responseStream->print('\\');
        responseStream->print(c);
      }
      responseStream->println("\"}");
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
      // Stream broadcast always goes to USB Serial for debugging,
      // regardless of where the last command came from.
      Stream* saved = responseStream;
      responseStream = &Serial;
      Serial.print("STATE:");
      sendStateJSON();
      responseStream = saved;
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
      responseStream->println("{\"ok\":false,\"reason\":\"tracking_active\"}");
      return false;
    }
    if (servos == nullptr || engine == nullptr) {
      responseStream->println("{\"ok\":false,\"reason\":\"not_initialized\"}");
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
      responseStream->println("{\"ok\":false,\"reason\":\"not_initialized\"}");
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

    responseStream->print("{\"arousal\":");
    responseStream->print(emo.getArousal(), 2);
    responseStream->print(",\"valence\":");
    responseStream->print(emo.getValence(), 2);
    responseStream->print(",\"dominance\":");
    responseStream->print(emo.getDominance(), 2);
    responseStream->print(",\"emotion\":\"");
    responseStream->print(emo.getLabelString());
    responseStream->print("\",\"behavior\":\"");
    responseStream->print(behaviorName(beh));
    responseStream->print("\",\"stimulation\":");
    responseStream->print(needs.getStimulation(), 2);
    responseStream->print(",\"social\":");
    responseStream->print(needs.getSocial(), 2);
    responseStream->print(",\"energy\":");
    responseStream->print(needs.getEnergy(), 2);
    responseStream->print(",\"safety\":");
    responseStream->print(needs.getSafety(), 2);
    responseStream->print(",\"novelty\":");
    responseStream->print(needs.getNovelty(), 2);
    responseStream->print(",\"tracking\":");
    responseStream->print(tracking ? "true" : "false");
    responseStream->print(",\"animating\":");
    responseStream->print(animating ? "true" : "false");
    responseStream->print(",\"servoBase\":");
    responseStream->print(base);
    responseStream->print(",\"servoNod\":");
    responseStream->print(nod);
    responseStream->print(",\"servoTilt\":");
    responseStream->print(tilt);

    // Consciousness state
    ConsciousnessLayer& consciousness = engine->getConsciousness();
    responseStream->print(",\"epistemic\":\"");
    switch(consciousness.getEpistemicState()) {
        case EPIST_CONFIDENT: responseStream->print("confident"); break;
        case EPIST_UNCERTAIN: responseStream->print("uncertain"); break;
        case EPIST_CONFUSED: responseStream->print("confused"); break;
        case EPIST_LEARNING: responseStream->print("learning"); break;
        case EPIST_CONFLICTED: responseStream->print("conflicted"); break;
        case EPIST_WONDERING: responseStream->print("wondering"); break;
    }
    responseStream->print("\"");
    responseStream->print(",\"tension\":");
    responseStream->print(consciousness.getTension(), 2);
    responseStream->print(",\"wondering\":");
    responseStream->print(consciousness.isWondering() ? "true" : "false");
    responseStream->print(",\"selfAwareness\":");
    responseStream->print(consciousness.getSelfAwareness(), 2);

    // Speech urge fields
    responseStream->print(",\"speechUrge\":");
    responseStream->print(engine->getSpeechUrge().getUrge(), 2);
    responseStream->print(",\"speechTrigger\":\"");
    responseStream->print(engine->getSpeechUrge().triggerToString());
    responseStream->print("\",\"wantsToSpeak\":");
    responseStream->print(engine->getSpeechUrge().wantsToSpeak() ? "true" : "false");

    responseStream->println("}");
  }

  // ============================================
  // !LOOK:base,nod - Move servos safely
  // ============================================

  void cmdLook(const char* args) {
    stopAIAnim();
    if (!checkServoAccess()) return;

    int base, nod;
    if (sscanf(args, "%d,%d", &base, &nod) != 2) {
      responseStream->println("{\"ok\":false,\"reason\":\"parse_error\"}");
      return;
    }

    base = constrain(base, 10, 170);
    nod = constrain(nod, 80, 150);

    MovementStyleParams style = engine->getMovementStyle();

    int curBase, curNod, curTilt;
    servos->getPosition(curBase, curNod, curTilt);
    servos->smoothMoveTo(base, nod, curTilt, style);

    responseStream->println("{\"ok\":true}");
  }

  // ============================================
  // !SATISFY:need,amount - Satisfy a homeostatic need
  // ============================================

  void cmdSatisfy(const char* args) {
    if (engine == nullptr) {
      responseStream->println("{\"ok\":false,\"reason\":\"not_initialized\"}");
      return;
    }

    char needName[16];
    float amount = 0.0;

    const char* comma = strchr(args, ',');
    if (comma == nullptr) {
      responseStream->println("{\"ok\":false,\"reason\":\"parse_error\"}");
      return;
    }

    int nameLen = comma - args;
    if (nameLen <= 0 || nameLen >= (int)sizeof(needName)) {
      responseStream->println("{\"ok\":false,\"reason\":\"parse_error\"}");
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
      responseStream->print("{\"ok\":false,\"reason\":\"unknown_need\",\"need\":\"");
      responseStream->print(needName);
      responseStream->println("\"}");
      return;
    }

    responseStream->print("{\"ok\":true,\"need\":\"");
    responseStream->print(needName);
    responseStream->print("\",\"value\":");
    responseStream->print(resultValue, 2);
    responseStream->println("}");
  }

  // ============================================
  // !PRESENCE - Simulate human presence detection
  // ============================================

  void cmdPresence() {
    if (engine == nullptr) {
      responseStream->println("{\"ok\":false,\"reason\":\"not_initialized\"}");
      return;
    }

    Needs& needs = engine->getNeeds();
    needs.detectHumanPresence();

    responseStream->println("{\"ok\":true}");
  }

  // ============================================
  // !EXPRESS:emotion - Express an emotion via animation
  // ============================================

  void cmdExpress(const char* args) {
    stopAIAnim();

    if (animator == nullptr || engine == nullptr) {
      responseStream->println("{\"ok\":false,\"reason\":\"not_initialized\"}");
      return;
    }

    if (animator->isCurrentlyAnimating()) {
      responseStream->println("{\"ok\":false,\"reason\":\"animating\"}");
      return;
    }

    if (reflex != nullptr && reflex->isActive()) {
      responseStream->println("{\"ok\":false,\"reason\":\"tracking_active\"}");
      return;
    }

    EmotionLabel label;
    if (!parseEmotionLabel(args, label)) {
      responseStream->print("{\"ok\":false,\"reason\":\"unknown_emotion\",\"emotion\":\"");
      responseStream->print(args);
      responseStream->println("\"}");
      return;
    }

    Personality& pers = engine->getPersonality();
    Needs& needs = engine->getNeeds();
    animator->expressEmotion(label, pers, needs);

    responseStream->println("{\"ok\":true}");
  }

  // ============================================
  // !NOD:count - Nod yes animation
  // ============================================

  void cmdNod(const char* args) {
    stopAIAnim();

    if (animator == nullptr || engine == nullptr) {
      responseStream->println("{\"ok\":false,\"reason\":\"not_initialized\"}");
      return;
    }

    if (animator->isCurrentlyAnimating()) {
      responseStream->println("{\"ok\":false,\"reason\":\"animating\"}");
      return;
    }

    if (reflex != nullptr && reflex->isActive()) {
      responseStream->println("{\"ok\":false,\"reason\":\"tracking_active\"}");
      return;
    }

    int count = atoi(args);
    if (count < 1) count = 1;
    if (count > 10) count = 10;

    Emotion& emo = engine->getEmotion();
    Personality& pers = engine->getPersonality();
    Needs& needs = engine->getNeeds();
    animator->nodYes(count, emo, pers, needs);

    responseStream->println("{\"ok\":true}");
  }

  // ============================================
  // !SHAKE:count - Shake no animation
  // ============================================

  void cmdShake(const char* args) {
    stopAIAnim();

    if (animator == nullptr || engine == nullptr) {
      responseStream->println("{\"ok\":false,\"reason\":\"not_initialized\"}");
      return;
    }

    if (animator->isCurrentlyAnimating()) {
      responseStream->println("{\"ok\":false,\"reason\":\"animating\"}");
      return;
    }

    if (reflex != nullptr && reflex->isActive()) {
      responseStream->println("{\"ok\":false,\"reason\":\"tracking_active\"}");
      return;
    }

    int count = atoi(args);
    if (count < 1) count = 1;
    if (count > 10) count = 10;

    Emotion& emo = engine->getEmotion();
    Personality& pers = engine->getPersonality();
    Needs& needs = engine->getNeeds();
    animator->shakeNo(count, emo, pers, needs);

    responseStream->println("{\"ok\":true}");
  }

  // ============================================
  // !STREAM:on/off - Toggle state streaming
  // ============================================

  void cmdStream(const char* args) {
    if (strcmp(args, "on") == 0) {
      streamingEnabled = true;
      lastStreamTime = millis();
      responseStream->println("{\"ok\":true,\"streaming\":true}");
    }
    else if (strcmp(args, "off") == 0) {
      streamingEnabled = false;
      responseStream->println("{\"ok\":true,\"streaming\":false}");
    }
    else {
      responseStream->println("{\"ok\":false,\"reason\":\"use_on_or_off\"}");
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
      responseStream->print("{\"ok\":false,\"reason\":\"unknown_direction\",\"dir\":\"");
      responseStream->print(args);
      responseStream->println("\"}");
      return;
    }

    MovementStyleParams style = engine->getMovementStyle();

    int curBase, curNod, curTilt;
    servos->getPosition(curBase, curNod, curTilt);
    servos->smoothMoveTo(base, nod, curTilt, style);

    responseStream->println("{\"ok\":true}");
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

    responseStream->println("{\"ok\":true}");
  }

  // ============================================
  // !THINKING - Start looping pondering animation
  // Non-blocking: sets mode, updateLoopingAnimation() drives it
  // ============================================

  void cmdThinking() {
    stopAIAnim();

    if (servos == nullptr) {
      responseStream->println("{\"ok\":false,\"reason\":\"not_initialized\"}");
      return;
    }

    // Don't start if reflex is actively tracking
    if (reflex != nullptr && reflex->isActive()) {
      responseStream->println("{\"ok\":false,\"reason\":\"tracking_active\"}");
      return;
    }

    aiAnimMode = AI_ANIM_THINKING;
    aiAnimStartTime = millis();
    lastAiAnimStep = 0;

    responseStream->println("{\"ok\":true}");
  }

  // ============================================
  // !STOP_THINKING - Stop thinking animation
  // ============================================

  void cmdStopThinking() {
    stopAIAnim();
    responseStream->println("{\"ok\":true}");
  }

  // ============================================
  // !SPEAKING - Start looping conversational animation
  // Non-blocking: sets mode, updateLoopingAnimation() drives it
  // ============================================

  void cmdSpeaking() {
    stopAIAnim();

    if (servos == nullptr) {
      responseStream->println("{\"ok\":false,\"reason\":\"not_initialized\"}");
      return;
    }

    if (reflex != nullptr && reflex->isActive()) {
      responseStream->println("{\"ok\":false,\"reason\":\"tracking_active\"}");
      return;
    }

    aiAnimMode = AI_ANIM_SPEAKING;
    aiAnimStartTime = millis();
    lastAiAnimStep = 0;

    responseStream->println("{\"ok\":true}");
  }

  // ============================================
  // !STOP_SPEAKING - Stop speaking animation
  // ============================================

  void cmdStopSpeaking() {
    stopAIAnim();
    responseStream->println("{\"ok\":true}");
  }

  // ============================================
  // !ACKNOWLEDGE - Quick subtle nod
  // Brief blocking (~150ms) - acceptable for one-shot
  // ============================================

  void cmdAcknowledge() {
    if (servos == nullptr) {
      responseStream->println("{\"ok\":false,\"reason\":\"not_initialized\"}");
      return;
    }

    if (reflex != nullptr && reflex->isActive()) {
      responseStream->println("{\"ok\":false,\"reason\":\"tracking_active\"}");
      return;
    }

    int base, nod, tilt;
    servos->getPosition(base, nod, tilt);

    // Quick small nod: down 8 degrees, then back
    int nodDown = constrain(nod + 8, 80, 150);
    servos->directWrite(base, nodDown, false);
    delay(120);
    servos->directWrite(base, nod, false);

    responseStream->println("{\"ok\":true}");
  }

  // ============================================
  // !CELEBRATE - Happy bounce animation
  // ============================================

  void cmdCelebrate() {
    stopAIAnim();

    if (animator == nullptr || engine == nullptr) {
      responseStream->println("{\"ok\":false,\"reason\":\"not_initialized\"}");
      return;
    }

    if (reflex != nullptr && reflex->isActive()) {
      responseStream->println("{\"ok\":false,\"reason\":\"tracking_active\"}");
      return;
    }

    Emotion& emo = engine->getEmotion();
    Personality& pers = engine->getPersonality();
    Needs& needs = engine->getNeeds();
    animator->playfulBounce(emo, pers, needs);

    responseStream->println("{\"ok\":true}");
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

    responseStream->println("{\"ok\":true}");
  }

  // ============================================
  // !SPOKE - Acknowledge that spontaneous speech happened (resets urge)
  // ============================================

  void cmdSpoke() {
    if (engine == nullptr) {
      responseStream->println("{\"ok\":false,\"reason\":\"not_initialized\"}");
      return;
    }
    engine->getSpeechUrge().utteranceCompleted();
    // Satisfy some stimulation need since Buddy "expressed itself"
    engine->getNeeds().satisfyStimulation(0.1f);
    responseStream->println("{\"ok\":true,\"action\":\"spoke_acknowledged\"}");
  }

  // ============================================
  // !VISION:json — Phase 2: Autonomous Observation Loop
  // Updates behavior engine with PC vision observations.
  // This is a ONE-WAY feed (no response) to avoid UART contention.
  // Called directly from parseVisionData() at 2-3 Hz.
  // ============================================

  public:
  void cmdVision(const char* jsonStr) {
    if (engine == nullptr) return;

    // Parse compact vision update from PC
    // Format: {"f":1,"fc":2,"ex":"happy","nv":0.45,"ob":3,"mv":0.2}
    int faceDetected = 0;
    int faceCount = 0;
    char expression[16] = "neutral";
    float sceneNovelty = 0.0;
    int objectCount = 0;
    float movement = 0.0;

    const char* p;

    p = strstr(jsonStr, "\"f\":");
    if (p) faceDetected = atoi(p + 4);

    p = strstr(jsonStr, "\"fc\":");
    if (p) faceCount = atoi(p + 5);

    p = strstr(jsonStr, "\"ex\":\"");
    if (p) {
        p += 6;
        int i = 0;
        while (*p && *p != '"' && i < 15) {
            expression[i++] = *p++;
        }
        expression[i] = '\0';
    }

    p = strstr(jsonStr, "\"nv\":");
    if (p) sceneNovelty = atof(p + 5);

    p = strstr(jsonStr, "\"ob\":");
    if (p) objectCount = atoi(p + 5);

    p = strstr(jsonStr, "\"mv\":");
    if (p) movement = atof(p + 5);

    // ── Feed into behavior engine ──

    SpatialMemory& spatialMemory = engine->getSpatialMemory();
    Emotion& emotion = engine->getEmotion();
    Needs& needs = engine->getNeeds();
    ConsciousnessLayer& consciousness = engine->getConsciousness();

    // 1. Scene novelty → spatial memory (enriches ultrasonic-only data)
    if (sceneNovelty > 0.0) {
        // Compute approximate direction from base servo angle
        int base = 90;
        if (servos != nullptr) {
            int b, n, t;
            servos->getPosition(b, n, t);
            base = b;
        }
        // Map servo angle to 8-bin direction: 90=front(0), >130=left(6), <50=right(2)
        int dir;
        if (base > 130)      dir = 6;  // Left
        else if (base > 110) dir = 7;  // Front-left
        else if (base > 70)  dir = 0;  // Front
        else if (base > 50)  dir = 1;  // Front-right
        else                 dir = 2;  // Right

        spatialMemory.injectExternalNovelty(dir, sceneNovelty);
    }

    // 2. Expression → emotional resonance
    if (faceDetected && strcmp(expression, "neutral") != 0) {
        float valenceShift = 0.0;
        float arousalShift = 0.0;

        if (strcmp(expression, "happy") == 0)          { valenceShift = 0.05;  arousalShift = 0.02; }
        else if (strcmp(expression, "surprised") == 0)  { arousalShift = 0.08; }
        else if (strcmp(expression, "frowning") == 0)   { valenceShift = -0.03; arousalShift = 0.02; }
        else if (strcmp(expression, "angry") == 0)      { valenceShift = -0.05; arousalShift = 0.05; }
        else if (strcmp(expression, "sad") == 0)        { valenceShift = -0.04; arousalShift = -0.02; }
        else if (strcmp(expression, "raised_brows") == 0) { arousalShift = 0.03; }

        emotion.nudge(valenceShift, arousalShift);
    }

    // 3. Face count → social context
    if (faceCount > 1) {
        needs.satisfySocial(0.02 * faceCount);
    }

    // 4. Object count + movement → stimulation
    if (objectCount > 0 || movement > 0.3) {
        float stimAmount = min(0.05f, movement * 0.03f + objectCount * 0.01f);
        needs.satisfyStimulation(stimAmount);
    }

    // 5. High novelty → consciousness event (can trigger wondering)
    if (sceneNovelty > 0.5) {
        consciousness.onEnvironmentChange(sceneNovelty);
    }

    // No response — this is a continuous feed, not a request/response command.
    // Saves UART bandwidth and avoids contention.
  }

  private:

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
