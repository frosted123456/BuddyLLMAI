// SpeechUrge.h
// Accumulates internal pressure to externalize — the "I want to say something" feeling.
//
// This is NOT a speech system. It's a motivation system.
// It answers: "How much does Buddy want to speak right now, and why?"
//
// Python reads the urge via QUERY and decides whether to let Buddy talk.

#ifndef SPEECH_URGE_H
#define SPEECH_URGE_H

#include "Needs.h"
#include "Emotion.h"
#include "Personality.h"

// Why Buddy wants to speak
enum SpeechTrigger {
  TRIGGER_NONE = 0,
  TRIGGER_LONELY,           // High social need, no one around
  TRIGGER_BORED,            // High stimulation need, nothing happening
  TRIGGER_WONDERING,        // In a wondering state, wants to think aloud
  TRIGGER_FACE_APPEARED,    // Someone just showed up
  TRIGGER_FACE_RECOGNIZED,  // Someone FAMILIAR just showed up
  TRIGGER_FACE_LEFT,        // Person just left
  TRIGGER_STARTLED,         // Sudden environmental change
  TRIGGER_CONTENT,          // Feeling good, wants to express it
  TRIGGER_CONFLICT,         // Internal conflict, thinking out loud
  TRIGGER_DISCOVERY,        // Found something interesting (novel stimulus)
  TRIGGER_GREETING,         // Morning/return greeting based on time
  TRIGGER_COMMENTARY        // Observing something worth commenting on
};

class SpeechUrgeSystem {
private:
  float urge;                     // 0.0 to 1.0 — pressure to speak
  SpeechTrigger currentTrigger;   // Why we want to speak
  float triggerIntensity;         // How strongly triggered (0-1)

  unsigned long lastUtterance;    // When Buddy last spoke (set by Python via command)
  unsigned long lastFaceTime;     // When we last saw a face
  unsigned long faceAppearedTime; // When current face first appeared
  bool facePresent;               // Is someone here right now?
  bool facePresentPrev;           // Was someone here last update?
  bool recognizedFace;            // Is the current face recognized?

  // Cooldown tracking
  unsigned long lastTriggerTime[13]; // Per-trigger cooldowns

  // Configuration
  static constexpr float URGE_THRESHOLD = 0.7f;       // Must exceed to signal readiness
  static constexpr float URGE_DECAY = 0.985f;          // Per-update decay (~1Hz)
  static constexpr unsigned long MIN_UTTERANCE_GAP = 120000;  // 2 minutes minimum
  static constexpr unsigned long GREETING_COOLDOWN = 300000;   // 5 min between greetings
  static constexpr unsigned long LONELY_ONSET = 180000;        // 3 min alone before lonely
  static constexpr unsigned long BORED_ONSET = 240000;         // 4 min idle before bored

public:
  SpeechUrgeSystem() {
    urge = 0.0f;
    currentTrigger = TRIGGER_NONE;
    triggerIntensity = 0.0f;
    lastUtterance = 0;
    lastFaceTime = 0;
    faceAppearedTime = 0;
    facePresent = false;
    facePresentPrev = false;
    recognizedFace = false;
    for (int i = 0; i < 13; i++) lastTriggerTime[i] = 0;
  }

  // ============================================
  // CALL EVERY SECOND (1Hz, same rate as consciousness)
  // ============================================
  void update(Needs& needs, Emotion& emotion, Personality& personality,
              bool isWondering, bool isInConflict, float conflictTension,
              bool faceDetected, bool faceIsRecognized,
              float environmentNovelty, unsigned long now) {

    // Don't build urge if we just spoke
    if (now - lastUtterance < MIN_UTTERANCE_GAP) {
      urge *= 0.95f;  // Faster decay during cooldown
      return;
    }

    // Track face transitions
    facePresentPrev = facePresent;
    facePresent = faceDetected;
    recognizedFace = faceIsRecognized;
    if (faceDetected) lastFaceTime = now;

    // ─── FACE APPEARED (someone just showed up) ───
    if (faceDetected && !facePresentPrev) {
      faceAppearedTime = now;
      float greetUrge = 0.0f;

      if (faceIsRecognized && (now - lastTriggerTime[TRIGGER_FACE_RECOGNIZED] > GREETING_COOLDOWN)) {
        greetUrge = 0.8f + personality.getSociability() * 0.2f;
        proposeTrigger(TRIGGER_FACE_RECOGNIZED, greetUrge, now);
      }
      else if (!faceIsRecognized && (now - lastTriggerTime[TRIGGER_FACE_APPEARED] > GREETING_COOLDOWN)) {
        greetUrge = 0.6f + personality.getCuriosity() * 0.2f;
        proposeTrigger(TRIGGER_FACE_APPEARED, greetUrge, now);
      }
    }

    // ─── FACE LEFT (person just walked away) ───
    if (!faceDetected && facePresentPrev) {
      if (now - lastTriggerTime[TRIGGER_FACE_LEFT] > 60000) {  // 1 min cooldown
        float leftUrge = 0.4f + personality.getSociability() * 0.3f;
        // Stronger if we were engaged for a while
        if (now - faceAppearedTime > 30000) leftUrge += 0.2f;  // 30s+ interaction
        proposeTrigger(TRIGGER_FACE_LEFT, leftUrge, now);
      }
    }

    // ─── LONELY (high social need, nobody around) ───
    if (!faceDetected && needs.getSocial() > 0.6f &&
        (now - lastFaceTime > LONELY_ONSET || lastFaceTime == 0)) {
      float lonelyUrge = needs.getSocial() * personality.getSociability() * 0.7f;
      if (now - lastTriggerTime[TRIGGER_LONELY] > 300000) {  // 5 min cooldown
        proposeTrigger(TRIGGER_LONELY, lonelyUrge, now);
      }
    }

    // ─── BORED (high stimulation need, nothing happening) ───
    if (needs.getStimulation() > 0.6f && environmentNovelty < 0.2f &&
        now - lastTriggerTime[TRIGGER_BORED] > 300000) {
      float boredUrge = needs.getStimulation() * personality.getCuriosity() * 0.6f;
      proposeTrigger(TRIGGER_BORED, boredUrge, now);
    }

    // ─── WONDERING (consciousness is in wondering state) ───
    if (isWondering && now - lastTriggerTime[TRIGGER_WONDERING] > 300000) {
      float wonderUrge = 0.5f + personality.getCuriosity() * 0.3f;
      proposeTrigger(TRIGGER_WONDERING, wonderUrge, now);
    }

    // ─── CONFLICT (internal tension wants to think aloud) ───
    if (isInConflict && conflictTension > 0.6f &&
        now - lastTriggerTime[TRIGGER_CONFLICT] > 180000) {
      float conflictUrge = conflictTension * 0.6f;
      proposeTrigger(TRIGGER_CONFLICT, conflictUrge, now);
    }

    // ─── STARTLED (sudden change) ───
    if (emotion.getArousal() > 0.8f && emotion.getValence() < -0.2f &&
        now - lastTriggerTime[TRIGGER_STARTLED] > 30000) {
      proposeTrigger(TRIGGER_STARTLED, 0.85f, now);
    }

    // ─── CONTENT (feeling good, sharing it) ───
    if (emotion.getValence() > 0.5f && emotion.getArousal() > 0.3f &&
        emotion.getArousal() < 0.6f &&
        now - lastTriggerTime[TRIGGER_CONTENT] > 300000) {
      float contentUrge = emotion.getValence() * personality.getSociability() * 0.5f;
      proposeTrigger(TRIGGER_CONTENT, contentUrge, now);
    }

    // ─── DISCOVERY (novel stimulus above threshold) ───
    if (environmentNovelty > 0.7f && now - lastTriggerTime[TRIGGER_DISCOVERY] > 120000) {
      float discoveryUrge = environmentNovelty * personality.getCuriosity() * 0.7f;
      proposeTrigger(TRIGGER_DISCOVERY, discoveryUrge, now);
    }

    // Natural decay
    urge *= URGE_DECAY;

    // Clear trigger if urge drops below noise floor
    if (urge < 0.1f) {
      currentTrigger = TRIGGER_NONE;
      triggerIntensity = 0.0f;
    }
  }

  // ============================================
  // TRIGGER PROPOSAL — Higher intensity wins
  // ============================================
  void proposeTrigger(SpeechTrigger trigger, float intensity, unsigned long now) {
    intensity = constrain(intensity, 0.0f, 1.0f);

    // New trigger wins if stronger than current
    if (intensity > triggerIntensity) {
      currentTrigger = trigger;
      triggerIntensity = intensity;
      urge = max(urge, intensity);  // Urge jumps to at least trigger level
    }
  }

  // ============================================
  // CALLED BY PYTHON (via AIBridge command) AFTER UTTERANCE
  // ============================================
  void utteranceCompleted() {
    lastUtterance = millis();
    if (currentTrigger != TRIGGER_NONE) {
      lastTriggerTime[currentTrigger] = millis();
    }
    urge = 0.0f;
    currentTrigger = TRIGGER_NONE;
    triggerIntensity = 0.0f;
  }

  // ============================================
  // GETTERS — Used by AIBridge QUERY response
  // ============================================
  bool wantsToSpeak() const { return urge >= URGE_THRESHOLD && currentTrigger != TRIGGER_NONE; }
  float getUrge() const { return urge; }
  SpeechTrigger getTrigger() const { return currentTrigger; }
  float getTriggerIntensity() const { return triggerIntensity; }
  bool isFacePresent() const { return facePresent; }

  const char* triggerToString() const {
    switch(currentTrigger) {
      case TRIGGER_NONE: return "none";
      case TRIGGER_LONELY: return "lonely";
      case TRIGGER_BORED: return "bored";
      case TRIGGER_WONDERING: return "wondering";
      case TRIGGER_FACE_APPEARED: return "face_appeared";
      case TRIGGER_FACE_RECOGNIZED: return "face_recognized";
      case TRIGGER_FACE_LEFT: return "face_left";
      case TRIGGER_STARTLED: return "startled";
      case TRIGGER_CONTENT: return "content";
      case TRIGGER_CONFLICT: return "conflict";
      case TRIGGER_DISCOVERY: return "discovery";
      case TRIGGER_GREETING: return "greeting";
      case TRIGGER_COMMENTARY: return "commentary";
      default: return "unknown";
    }
  }
};

#endif // SPEECH_URGE_H
