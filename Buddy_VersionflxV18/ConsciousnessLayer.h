// ConsciousnessLayer.h
// The phenomenology of experience — what gives Buddy an inner life
//
// This isn't a behavior selector. It's a layer that creates TEXTURE:
// - Decisions feel conflicted (not clean)
// - Timing feels variable (not metronomic)
// - Self-awareness creates visible "catching oneself"
// - Wondering creates moments of purposeless contemplation
//
// Memory: ~2KB RAM, ~0.1ms per update

#ifndef CONSCIOUSNESS_LAYER_H
#define CONSCIOUSNESS_LAYER_H

#include "BehaviorSelection.h"
#include "Emotion.h"
#include "Personality.h"
#include "Needs.h"
#include "SpatialMemory.h"
#include "Learning.h"

// ============================================
// EPISTEMIC STATES — What do I know?
// ============================================
enum EpistemicState {
    EPIST_CONFIDENT,     // I know what's going on
    EPIST_UNCERTAIN,     // Could go either way
    EPIST_CONFUSED,      // This doesn't add up
    EPIST_LEARNING,      // Actively figuring it out
    EPIST_CONFLICTED,    // Two interpretations clash
    EPIST_WONDERING      // Existential/reflective state
};

// ============================================
// MOTIVATIONAL CONFLICT — Competing drives
// ============================================
struct MotivationalTension {
    Behavior dominantDrive;
    Behavior suppressedDrive;
    float tensionLevel;          // 0-1: how hard is the conflict?
    float suppressionCost;       // 0-1: mental effort to resist
    unsigned long conflictStart;

    bool inConflict() const { return tensionLevel > 0.3; }
    float duration() const { return (millis() - conflictStart) / 1000.0; }
};

// ============================================
// SELF-NARRATIVE — Story of me
// ============================================
struct SelfNarrative {
    // Identity
    float perceivedCompetence;   // "I'm good at this"
    float perceivedSafety;       // "My world is safe"
    float socialConfidence;      // "People respond to me"

    // Emotional trajectory
    float recentMoodTrend;       // positive = improving, negative = declining
    float moodTrendSamples[8];   // rolling window
    int moodSampleIndex;

    // Spatial preferences (developed over time)
    float directionPreferences[8]; // -1 to +1 per direction

    // What just happened
    Behavior lastSignificantAction;
    float lastActionOutcome;
    unsigned long lastSignificantTime;
};

// ============================================
// COUNTERFACTUAL THOUGHT — What if?
// ============================================
struct CounterfactualThought {
    bool active;
    Behavior actualAction;
    Behavior imaginedAlternative;
    float predictedOutcome;
    float regret;               // 0-1: wish I'd done differently
    float relief;               // 0-1: glad I didn't
    unsigned long startTime;
};

// ============================================
// WONDERING STATE — Existential moments
// ============================================
enum WonderingType {
    WONDER_SELF,          // Who am I?
    WONDER_PLACE,         // What is this place?
    WONDER_PURPOSE,       // Why do I do this?
    WONDER_FUTURE,        // What happens next?
    WONDER_PAST           // What was that about?
};

struct WonderingState {
    bool isWondering;
    WonderingType type;
    float intensity;              // Fluctuates during wondering
    unsigned long startTime;
    unsigned long lastWondering;  // Prevent too-frequent wondering
};

// ============================================
// META-AWARENESS — Watching myself think
// ============================================
struct MetaAwareness {
    float selfAwareness;         // 0-1: how self-aware right now
    float awarenessOfUncertainty; // Noticing own confusion
    bool caughtMyself;           // "Wait, why am I doing this?"
    unsigned long lastCatch;
};

// ============================================
// CONSCIOUSNESS LAYER
// ============================================

class ConsciousnessLayer {
private:
    EpistemicState epistemicState;
    float subjectiveConfidence;

    MotivationalTension conflict;
    SelfNarrative narrative;
    CounterfactualThought counterfactual;
    WonderingState wondering;
    MetaAwareness meta;

public:
    ConsciousnessLayer() {
        epistemicState = EPIST_CONFIDENT;
        subjectiveConfidence = 0.7;

        conflict.tensionLevel = 0.0;
        conflict.suppressionCost = 0.0;
        conflict.dominantDrive = IDLE;
        conflict.suppressedDrive = IDLE;
        conflict.conflictStart = 0;

        narrative.perceivedCompetence = 0.5;
        narrative.perceivedSafety = 0.7;
        narrative.socialConfidence = 0.5;
        narrative.recentMoodTrend = 0.0;
        narrative.moodSampleIndex = 0;
        narrative.lastSignificantAction = IDLE;
        narrative.lastActionOutcome = 0.5;
        narrative.lastSignificantTime = 0;
        for (int i = 0; i < 8; i++) {
            narrative.moodTrendSamples[i] = 0.0;
            narrative.directionPreferences[i] = 0.0;
        }

        counterfactual.active = false;
        counterfactual.regret = 0.0;
        counterfactual.relief = 0.0;

        wondering.isWondering = false;
        wondering.intensity = 0.0;
        wondering.lastWondering = 0;

        meta.selfAwareness = 0.5;
        meta.awarenessOfUncertainty = 0.0;
        meta.caughtMyself = false;
        meta.lastCatch = 0;
    }

    // ========================================================================
    // MAIN UPDATE — Called from BehaviorEngine::mediumUpdate() (every 5s)
    // ========================================================================

    void update(BehaviorScore scores[], int numBehaviors,
                Needs& needs, Emotion& emotion, Personality& personality,
                SpatialMemory& memory, float deltaTime) {

        updateEpistemicState(memory, emotion);
        updateMotivationalConflict(scores, numBehaviors, needs, personality);
        updateSelfNarrative(emotion, needs, personality, deltaTime);
        updateCounterfactual(deltaTime);
        updateWondering(needs, emotion);
        updateMetaAwareness(emotion, personality);
        updatePreferences(memory, emotion);
    }

    // ========================================================================
    // EPISTEMIC STATE
    // ========================================================================

    void updateEpistemicState(SpatialMemory& memory, Emotion& emotion) {
        float novelty = memory.getTotalNovelty();
        float dynamism = memory.getAverageDynamism();
        float emotionalClarity = abs(emotion.getValence());

        if (conflict.inConflict()) {
            epistemicState = EPIST_CONFLICTED;
            subjectiveConfidence = 0.3;
        }
        else if (wondering.isWondering) {
            epistemicState = EPIST_WONDERING;
            subjectiveConfidence = 0.5;
        }
        else if (novelty > 0.7 && dynamism > 0.5) {
            epistemicState = EPIST_LEARNING;
            subjectiveConfidence = 0.4;
        }
        else if (novelty > 0.5 || emotionalClarity < 0.15) {
            epistemicState = EPIST_UNCERTAIN;
            subjectiveConfidence = 0.5;
        }
        else if (dynamism > 0.6 && emotion.getArousal() > 0.6) {
            epistemicState = EPIST_CONFUSED;
            subjectiveConfidence = 0.3;
        }
        else {
            epistemicState = EPIST_CONFIDENT;
            subjectiveConfidence = 0.8;
        }
    }

    // ========================================================================
    // MOTIVATIONAL CONFLICT — The heart of visible inner life
    // ========================================================================

    void updateMotivationalConflict(BehaviorScore scores[], int numBehaviors,
                                     Needs& needs, Personality& personality) {
        if (numBehaviors < 2) {
            conflict.tensionLevel = 0;
            return;
        }

        // Find top two behaviors
        Behavior first = scores[0].type;
        float firstScore = scores[0].finalScore;
        Behavior second = scores[1].type;
        float secondScore = scores[1].finalScore;

        for (int i = 2; i < numBehaviors; i++) {
            if (scores[i].finalScore > firstScore) {
                second = first; secondScore = firstScore;
                first = scores[i].type; firstScore = scores[i].finalScore;
            } else if (scores[i].finalScore > secondScore) {
                second = scores[i].type; secondScore = scores[i].finalScore;
            }
        }

        // Tension = how close the top two are
        float gap = firstScore - secondScore;
        float maxScore = max(firstScore, 0.01f);
        float rawTension = 1.0 - (gap / maxScore);

        // Amplify tension for opposing behaviors
        if ((first == EXPLORE && second == RETREAT) ||
            (first == RETREAT && second == EXPLORE) ||
            (first == SOCIAL_ENGAGE && second == RETREAT) ||
            (first == RETREAT && second == SOCIAL_ENGAGE) ||
            (first == PLAY && second == REST) ||
            (first == REST && second == PLAY)) {
            rawTension *= 1.4;  // Opposing drives create MORE tension
        }

        // Personality modulates felt tension
        rawTension *= (0.7 + personality.getCaution() * 0.6);
        rawTension *= (1.3 - personality.getPlayfulness() * 0.3);

        conflict.tensionLevel = constrain(rawTension, 0.0, 1.0);
        conflict.dominantDrive = first;
        conflict.suppressedDrive = second;

        // Suppression cost builds over time during conflict
        if (conflict.inConflict()) {
            if (conflict.conflictStart == 0) conflict.conflictStart = millis();
            float duration = conflict.duration();
            conflict.suppressionCost = constrain(duration * 0.1, 0.0, 0.8);
        } else {
            conflict.conflictStart = 0;
            conflict.suppressionCost *= 0.9;  // Decay
        }
    }

    // ========================================================================
    // COUNTERFACTUAL THINKING — "What if I'd done the other thing?"
    // ========================================================================

    void triggerCounterfactual(Behavior actual, Behavior alternative, float outcome) {
        counterfactual.active = true;
        counterfactual.actualAction = actual;
        counterfactual.imaginedAlternative = alternative;
        counterfactual.startTime = millis();

        // Imagine: would the alternative have been better?
        float imaginaryOutcome = outcome + (random(-30, 30) / 100.0);
        counterfactual.predictedOutcome = constrain(imaginaryOutcome, 0.0, 1.0);

        if (imaginaryOutcome > outcome + 0.1) {
            counterfactual.regret = (imaginaryOutcome - outcome);
        } else {
            counterfactual.regret = 0.0;
        }

        if (outcome > imaginaryOutcome + 0.1) {
            counterfactual.relief = (outcome - imaginaryOutcome);
        } else {
            counterfactual.relief = 0.0;
        }
    }

    void updateCounterfactual(float dt) {
        if (!counterfactual.active) return;

        // Counterfactual thinking lasts 3-5 seconds
        if (millis() - counterfactual.startTime > 4000) {
            counterfactual.active = false;
            counterfactual.regret *= 0.5;  // Regret fades
            counterfactual.relief *= 0.5;
        }
    }

    // ========================================================================
    // WONDERING — Existential contemplation
    // ========================================================================

    void updateWondering(Needs& needs, Emotion& emotion) {
        if (wondering.isWondering) {
            float duration = (millis() - wondering.startTime) / 1000.0;

            // Wondering intensity fluctuates (like real contemplation)
            wondering.intensity = 0.5 + sin(duration * 0.5) * 0.3;

            // End conditions: too long, needs arise, external stimulus
            if (duration > 45.0 || needs.getImbalance() > 0.5) {
                wondering.isWondering = false;
                wondering.intensity = 0.0;
            }
            return;
        }

        // Entry conditions: peaceful, satisfied, rare
        unsigned long timeSinceLast = millis() - wondering.lastWondering;
        if (timeSinceLast < 300000) return;  // 5 min minimum between

        if (needs.getImbalance() < 0.15 &&
            emotion.isCalm() &&
            emotion.getValence() > -0.2 &&
            needs.getSafety() > 0.7 &&
            random(10000) < 2) {  // Very rare: ~0.02% chance per update

            wondering.isWondering = true;
            wondering.startTime = millis();
            wondering.lastWondering = millis();
            wondering.intensity = 0.6;
            wondering.type = (WonderingType)random(0, 5);
        }
    }

    // ========================================================================
    // META-AWARENESS — Catching myself
    // ========================================================================

    void updateMetaAwareness(Emotion& emotion, Personality& personality) {
        // Self-awareness fluctuates
        float targetAwareness = 0.3 + personality.getCuriosity() * 0.3;

        // High arousal reduces self-awareness (action mode)
        targetAwareness -= emotion.getArousal() * 0.2;

        // Conflict increases self-awareness (noticing internal state)
        if (conflict.inConflict()) {
            targetAwareness += conflict.tensionLevel * 0.3;
        }

        meta.selfAwareness += (targetAwareness - meta.selfAwareness) * 0.1;
        meta.selfAwareness = constrain(meta.selfAwareness, 0.0, 1.0);

        // "Catching myself" — rare moment of self-interruption
        meta.caughtMyself = false;
        if (meta.selfAwareness > 0.6 && random(1000) < 5) {
            unsigned long sinceLastCatch = millis() - meta.lastCatch;
            if (sinceLastCatch > 60000) {  // Max once per minute
                meta.caughtMyself = true;
                meta.lastCatch = millis();
            }
        }

        // Awareness of own uncertainty
        if (epistemicState == EPIST_UNCERTAIN || epistemicState == EPIST_CONFUSED) {
            meta.awarenessOfUncertainty += 0.05;
        } else {
            meta.awarenessOfUncertainty *= 0.95;
        }
        meta.awarenessOfUncertainty = constrain(meta.awarenessOfUncertainty, 0.0, 1.0);
    }

    // ========================================================================
    // SELF-NARRATIVE — Building a story
    // ========================================================================

    void updateSelfNarrative(Emotion& emotion, Needs& needs,
                              Personality& personality, float dt) {
        // Track mood trend (rolling average of valence)
        narrative.moodTrendSamples[narrative.moodSampleIndex] = emotion.getValence();
        narrative.moodSampleIndex = (narrative.moodSampleIndex + 1) % 8;

        float sum = 0;
        for (int i = 0; i < 8; i++) sum += narrative.moodTrendSamples[i];
        float avgMood = sum / 8.0;
        narrative.recentMoodTrend = avgMood - emotion.getValence();  // trend direction

        // Update self-assessments slowly
        float safetyReading = needs.getSafety();
        narrative.perceivedSafety += (safetyReading - narrative.perceivedSafety) * 0.02;
    }

    void recordSignificantAction(Behavior action, float outcome) {
        narrative.lastSignificantAction = action;
        narrative.lastActionOutcome = outcome;
        narrative.lastSignificantTime = millis();

        // Update competence based on outcomes
        if (outcome > 0.6) {
            narrative.perceivedCompetence += 0.02;
        } else if (outcome < 0.3) {
            narrative.perceivedCompetence -= 0.01;
        }
        narrative.perceivedCompetence = constrain(narrative.perceivedCompetence, 0.1, 0.9);
    }

    void recordSocialOutcome(float quality) {
        narrative.socialConfidence += (quality - 0.5) * 0.05;
        narrative.socialConfidence = constrain(narrative.socialConfidence, 0.1, 0.9);
    }

    // ========================================================================
    // PREFERENCE DEVELOPMENT — Learned spatial likes/dislikes
    // ========================================================================

    void updatePreferences(SpatialMemory& memory, Emotion& emotion) {
        for (int dir = 0; dir < 8; dir++) {
            float novelty = memory.getNovelty(dir);
            float valence = emotion.getValence();

            if (novelty > 0.3) {
                narrative.directionPreferences[dir] += valence * 0.005;
                narrative.directionPreferences[dir] = constrain(
                    narrative.directionPreferences[dir], -0.5, 0.5);
            }
        }
    }

    // ========================================================================
    // BEHAVIOR MODULATION — How consciousness affects decisions
    // ========================================================================

    int getDeliberationDelay() {
        if (!conflict.inConflict()) return 0;
        return (int)(conflict.tensionLevel * 800);  // 0-800ms hesitation
    }

    bool shouldShowFalseStart() {
        return conflict.tensionLevel > 0.5 && random(100) < 30;
    }

    float getDirectionBias(int direction) {
        if (direction < 0 || direction >= 8) return 0.0;
        return narrative.directionPreferences[direction];
    }

    // ========================================================================
    // GETTERS
    // ========================================================================

    bool isWondering() const { return wondering.isWondering; }
    WonderingType getWonderingType() const { return wondering.type; }
    float getWonderingIntensity() const { return wondering.intensity; }

    bool isInConflict() const { return conflict.inConflict(); }
    float getTension() const { return conflict.tensionLevel; }
    Behavior getSuppressedDrive() const { return conflict.suppressedDrive; }
    Behavior getDominantDrive() const { return conflict.dominantDrive; }

    bool isCounterfactualThinking() const { return counterfactual.active; }
    float getRegret() const { return counterfactual.regret; }
    float getRelief() const { return counterfactual.relief; }
    const CounterfactualThought& getCounterfactual() const { return counterfactual; }

    bool didCatchMyself() const { return meta.caughtMyself; }
    float getSelfAwareness() const { return meta.selfAwareness; }

    EpistemicState getEpistemicState() const { return epistemicState; }
    float getSubjectiveConfidence() const { return subjectiveConfidence; }

    const SelfNarrative& getNarrative() const { return narrative; }
    const MotivationalTension& getConflict() const { return conflict; }

    // ========================================================================
    // DIAGNOSTICS
    // ========================================================================

    void printDiagnostics() {
        Serial.println("\n=== CONSCIOUSNESS STATE ===");

        Serial.print("  Epistemic: ");
        switch(epistemicState) {
            case EPIST_CONFIDENT:  Serial.println("CONFIDENT"); break;
            case EPIST_UNCERTAIN:  Serial.println("UNCERTAIN"); break;
            case EPIST_CONFUSED:   Serial.println("CONFUSED"); break;
            case EPIST_LEARNING:   Serial.println("LEARNING"); break;
            case EPIST_CONFLICTED: Serial.println("CONFLICTED"); break;
            case EPIST_WONDERING:  Serial.println("WONDERING"); break;
        }

        Serial.print("  Confidence: "); Serial.println(subjectiveConfidence, 2);
        Serial.print("  Self-awareness: "); Serial.println(meta.selfAwareness, 2);

        if (conflict.inConflict()) {
            Serial.print("  CONFLICT: tension=");
            Serial.print(conflict.tensionLevel, 2);
            Serial.print(" for ");
            Serial.print(conflict.duration(), 1);
            Serial.println("s");
        }

        if (wondering.isWondering) {
            Serial.print("  WONDERING: ");
            switch(wondering.type) {
                case WONDER_SELF: Serial.println("Who am I?"); break;
                case WONDER_PLACE: Serial.println("What is this place?"); break;
                case WONDER_PURPOSE: Serial.println("Why do I do this?"); break;
                case WONDER_FUTURE: Serial.println("What happens next?"); break;
                case WONDER_PAST: Serial.println("What was that about?"); break;
            }
        }

        if (counterfactual.active) {
            Serial.println("  Imagining alternatives...");
            if (counterfactual.regret > 0.1) {
                Serial.print("  Regret: "); Serial.println(counterfactual.regret, 2);
            }
        }

        Serial.print("  Mood trend: ");
        Serial.println(narrative.recentMoodTrend > 0.05 ? "improving" :
                       narrative.recentMoodTrend < -0.05 ? "declining" : "stable");
    }
};

#endif // CONSCIOUSNESS_LAYER_H
