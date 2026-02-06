// ConsciousnessManifest.h
// Physical manifestation of consciousness states
// Makes internal conflict, wondering, and self-awareness VISIBLE

#ifndef CONSCIOUSNESS_MANIFEST_H
#define CONSCIOUSNESS_MANIFEST_H

#include "ConsciousnessLayer.h"
#include "ServoController.h"
#include "BodySchema.h"
#include "Emotion.h"
#include "Personality.h"
#include "Needs.h"
#include "LittleBots_Board_Pins.h"

class ConsciousnessManifest {
public:

    // ========================================================================
    // WONDERING — Slow, purposeless contemplation
    // ========================================================================

    void manifestWondering(WonderingType type, float intensity,
                           ServoController& servos, Emotion& emotion,
                           Personality& personality, Needs& needs) {

        int base, nod, tilt;
        servos.getPosition(base, nod, tilt);

        // Very slow, dreamy movements
        MovementStyleParams style;
        style.speed = 0.2 + intensity * 0.1;
        style.smoothness = 0.9;
        style.hesitation = 0.0;
        style.delayMs = 30;
        style.amplitude = 0.3;
        style.directness = 0.5;
        style.rangeScale = 50;

        switch(type) {
            case WONDER_SELF:
                // Look down slightly, then slowly tilt head — introspective
                servos.smoothMoveTo(base, constrain(nod - 8, 80, 150),
                                     constrain(tilt - 10, 20, 150), style);
                break;

            case WONDER_PLACE:
                // Slow panoramic gaze — taking in surroundings
                {
                    int slowGaze = base + (int)(sin(millis() / 3000.0) * 20);
                    servos.smoothMoveTo(constrain(slowGaze, 15, 165), nod, tilt, style);
                }
                break;

            case WONDER_PURPOSE:
                // Small head tilt, slight pause — philosophical
                servos.smoothMoveTo(base, nod,
                                     constrain(tilt + (int)(sin(millis() / 2000.0) * 8), 20, 150),
                                     style);
                break;

            case WONDER_FUTURE:
                // Gaze slightly upward — looking toward the future
                servos.smoothMoveTo(base, constrain(nod + 5, 80, 150), tilt, style);
                break;

            case WONDER_PAST:
                // Gaze down-left — remembering
                servos.smoothMoveTo(constrain(base - 15, 15, 165),
                                     constrain(nod - 5, 80, 150), tilt, style);
                break;
        }
    }

    // ========================================================================
    // CONFLICT — Visible hesitation between two drives
    // ========================================================================

    void manifestConflict(const MotivationalTension& conflict,
                          ServoController& servos, BodySchema& bodySchema,
                          Emotion& emotion, Personality& personality, Needs& needs) {

        if (!conflict.inConflict()) return;

        int base, nod, tilt;
        servos.getPosition(base, nod, tilt);

        MovementStyleParams style;
        style.speed = 0.4;
        style.smoothness = 0.5;
        style.hesitation = conflict.tensionLevel * 0.5;
        style.delayMs = 15;
        style.amplitude = 0.5;
        style.directness = 0.3;
        style.rangeScale = 60;

        // Brief movement toward suppressed drive, then correction
        int falseStartBase = base;
        int falseStartNod = nod;

        switch(conflict.suppressedDrive) {
            case EXPLORE: falseStartBase += random(-15, 16); break;
            case RETREAT: falseStartNod -= 8; break;
            case SOCIAL_ENGAGE: falseStartNod += 5; break;
            case PLAY: falseStartBase += random(-10, 11); falseStartNod += 5; break;
            default: break;
        }

        falseStartBase = constrain(falseStartBase, 15, 165);
        falseStartNod = constrain(falseStartNod, 80, 150);

        // Quick false start
        style.speed = 0.7;
        servos.smoothMoveTo(falseStartBase, falseStartNod, tilt, style);

        // Pause (visible decision moment)
        delay(100 + (int)(conflict.tensionLevel * 300));

        // Correct back
        style.speed = 0.5;
        servos.smoothMoveTo(base, nod, tilt, style);
    }

    // ========================================================================
    // META-AWARENESS — "Catching myself"
    // ========================================================================

    void manifestMetaCatch(ServoController& servos, Emotion& emotion,
                           Personality& personality, Needs& needs) {

        int base, nod, tilt;
        servos.getPosition(base, nod, tilt);

        // Quick "snap back" — small upward jerk
        MovementStyleParams quickStyle;
        quickStyle.speed = 0.9;
        quickStyle.smoothness = 0.3;
        quickStyle.hesitation = 0.0;
        quickStyle.delayMs = 8;
        quickStyle.amplitude = 0.4;
        quickStyle.directness = 0.8;
        quickStyle.rangeScale = 40;

        int alertNod = constrain(nod + 6, 80, 150);
        servos.smoothMoveTo(base, alertNod, tilt, quickStyle);

        delay(200);

        // Subtle head tilt — "hmm, what was I doing?"
        int thinkTilt = constrain(tilt + random(-8, 9), 20, 150);
        quickStyle.speed = 0.5;
        servos.smoothMoveTo(base, nod, thinkTilt, quickStyle);

        // Brief sound
        tone(buzzerPin, 600); delay(40);
        tone(buzzerPin, 800); delay(60);
        noTone(buzzerPin);
    }

    // ========================================================================
    // COUNTERFACTUAL — Mental replay visible as subtle re-enactment
    // ========================================================================

    void manifestCounterfactual(const CounterfactualThought& cf,
                                 ServoController& servos, int currentDirection) {

        if (!cf.active) return;

        int base, nod, tilt;
        servos.getPosition(base, nod, tilt);

        MovementStyleParams style;
        style.speed = 0.3;
        style.smoothness = 0.8;
        style.hesitation = 0.1;
        style.delayMs = 20;
        style.amplitude = 0.3;
        style.directness = 0.4;
        style.rangeScale = 40;

        // Subtle look toward "what might have been"
        int glanceOffset = (cf.regret > 0.2) ? random(-10, 11) : 0;
        int glanceBase = constrain(base + glanceOffset, 15, 165);
        servos.smoothMoveTo(glanceBase, nod, tilt, style);

        delay(300);

        // Return with either a small nod (relief) or slight droop (regret)
        if (cf.relief > 0.2) {
            int nodRelief = constrain(nod + 3, 80, 150);
            servos.smoothMoveTo(base, nodRelief, tilt, style);
            delay(200);
        } else if (cf.regret > 0.2) {
            int nodRegret = constrain(nod - 3, 80, 150);
            servos.smoothMoveTo(base, nodRegret, tilt, style);
            delay(300);
        }

        servos.smoothMoveTo(base, nod, tilt, style);
    }

    // ========================================================================
    // EPISTEMIC EXPRESSION — Showing knowledge state
    // ========================================================================

    void manifestEpistemicState(EpistemicState state, float confidence,
                                 ServoController& servos) {
        int base, nod, tilt;
        servos.getPosition(base, nod, tilt);

        switch(state) {
            case EPIST_CONFUSED:
                tilt = constrain(tilt + 8, 20, 150);
                nod = constrain(nod - 3, 80, 150);
                break;

            case EPIST_LEARNING:
                nod = constrain(nod + 4, 80, 150);
                break;

            case EPIST_UNCERTAIN:
                tilt = constrain(tilt + (int)(sin(millis() / 800.0) * 4), 20, 150);
                break;

            default:
                return;
        }

        MovementStyleParams gentle;
        gentle.speed = 0.3;
        gentle.smoothness = 0.9;
        gentle.hesitation = 0.0;
        gentle.delayMs = 25;
        gentle.amplitude = 0.3;
        gentle.directness = 0.5;
        gentle.rangeScale = 40;
        servos.smoothMoveTo(base, nod, tilt, gentle);
    }
};

#endif // CONSCIOUSNESS_MANIFEST_H
