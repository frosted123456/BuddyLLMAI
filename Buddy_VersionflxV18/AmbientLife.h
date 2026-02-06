// AmbientLife.h
// Need-driven micro-movements that make Buddy appear alive
// NOT timer-based — driven by internal state

#ifndef AMBIENT_LIFE_H
#define AMBIENT_LIFE_H

#include "Needs.h"
#include "Emotion.h"
#include "Personality.h"
#include "ServoController.h"

class AmbientLife {
private:
    unsigned long lastBreath;
    unsigned long lastShift;
    unsigned long lastGlance;
    float breathPhase;

public:
    AmbientLife() : lastBreath(0), lastShift(0), lastGlance(0), breathPhase(0) {}

    // Called every update cycle when NOT tracking and NOT animating
    void update(Needs& needs, Emotion& emotion, Personality& personality,
                ServoController& servos, unsigned long now) {

        // === BREATHING ===
        // Always present. Amplitude scales with arousal.
        float breathRate = 4000 + (1.0 - emotion.getArousal()) * 3000; // 4-7s period
        float amplitude = 2.0 + emotion.getArousal() * 1.5;           // 2-3.5 degrees

        unsigned long elapsed = now - lastBreath;
        if (elapsed > 0) {
            breathPhase += elapsed / breathRate * TWO_PI;
            if (breathPhase > TWO_PI) breathPhase -= TWO_PI;
        }
        lastBreath = now;

        float breathOffset = sin(breathPhase) * amplitude;
        int currentNod = servos.getNodPos();
        int breathNod = constrain(currentNod + (int)breathOffset, 80, 150);
        // Apply breathing as a subtle offset — update internal state
        servos.updateState(servos.getBasePos(), breathNod, servos.getTiltPos());

        // === WEIGHT SHIFT ===
        // Driven by stimulation need. Bored -> shift more often.
        float stimPressure = needs.getStimulationPressure();
        float shiftInterval = 30000 - stimPressure * 20000;  // 10-30s based on boredom
        if (shiftInterval < 8000.0f) shiftInterval = 8000.0f;

        if (now - lastShift > (unsigned long)shiftInterval) {
            lastShift = now;

            int shiftAmount;
            if (needs.getEnergy() < 0.3) {
                // Low energy: droop slightly
                shiftAmount = random(-2, 3);
                int droopNod = constrain(servos.getNodPos() - 3, 80, 150);
                servos.updateState(servos.getBasePos(), droopNod, servos.getTiltPos());
            } else {
                // Normal: subtle weight shift
                shiftAmount = random(-5, 6);
            }

            int newBase = constrain(servos.getBasePos() + shiftAmount, 15, 165);
            servos.updateState(newBase, servos.getNodPos(), servos.getTiltPos());
        }

        // === CURIOUS GLANCE ===
        // Driven by novelty need. High novelty need -> glance more.
        float noveltyPressure = 1.0 - needs.getNovelty();
        float glanceInterval = 45000 - noveltyPressure * 30000
                               - personality.getCuriosity() * 10000; // 5-45s
        if (glanceInterval < 5000.0f) glanceInterval = 5000.0f;

        if (now - lastGlance > (unsigned long)glanceInterval) {
            lastGlance = now;

            // Quick glance — adjust tilt briefly for a "noticing" effect
            int currentTilt = servos.getTiltPos();
            int glanceTilt = constrain(currentTilt + random(-10, 11), 20, 150);
            servos.updateState(servos.getBasePos(), servos.getNodPos(), glanceTilt);
        }
    }
};

#endif // AMBIENT_LIFE_H
