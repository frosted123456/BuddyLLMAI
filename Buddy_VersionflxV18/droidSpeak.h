#ifndef DROID_SPEAK_H
#define DROID_SPEAK_H

#include "LittleBots_Board_Pins.h"

// Original droid speak function (preserved for backward compatibility)
void droidSpeak(int soundPin, int maxWords) {
  int toneDuration;
  int numberOfWords;
  int toneFreq;

  numberOfWords = random(1, maxWords);

  Serial.print("[SOUND] Droid speak: ");
  Serial.print(numberOfWords);
  Serial.println(" beeps");

  for (int i = 0; i <= numberOfWords; i++) {
    toneDuration = random(50, 300);
    toneFreq = random(200, 1500);
    tone(soundPin, toneFreq);
    delay(toneDuration);
    noTone(soundPin);
  }
}

namespace DroidSpeak {

    void startup() {
        int f[] = {300, 400, 500, 700, 900, 1100};
        int d[] = {80, 60, 60, 80, 60, 120};
        for (int i = 0; i < 6; i++) { tone(buzzerPin, f[i]); delay(d[i]); }
        noTone(buzzerPin);
    }

    void acknowledged() {
        tone(buzzerPin, 800); delay(60);
        tone(buzzerPin, 1200); delay(80);
        noTone(buzzerPin);
    }

    void thinkingPulse() {
        tone(buzzerPin, 350 + random(-30, 30)); delay(100);
        noTone(buzzerPin);
    }

    void happy() {
        tone(buzzerPin, 600); delay(80);
        tone(buzzerPin, 900); delay(80);
        tone(buzzerPin, 1200); delay(120);
        noTone(buzzerPin);
    }

    void sad() {
        for (int f = 600; f > 300; f -= 50) { tone(buzzerPin, f); delay(100); }
        noTone(buzzerPin);
    }

    void alert() {
        tone(buzzerPin, 1000); delay(50);
        noTone(buzzerPin); delay(50);
        tone(buzzerPin, 1200); delay(80);
        noTone(buzzerPin);
    }

    void sleepy() {
        for (int f = 500; f > 200; f -= 30) { tone(buzzerPin, f); delay(80); }
        noTone(buzzerPin);
    }

    void wondering() {
        // Soft, wandering tones — matches consciousness wondering state
        tone(buzzerPin, 400); delay(200);
        tone(buzzerPin, 500); delay(150);
        noTone(buzzerPin); delay(100);
        tone(buzzerPin, 350); delay(250);
        noTone(buzzerPin);
    }

    void conflicted() {
        // Two competing tones
        tone(buzzerPin, 600); delay(80);
        tone(buzzerPin, 400); delay(80);
        tone(buzzerPin, 550); delay(60);
        noTone(buzzerPin);
    }

    void chirp() {
        tone(buzzerPin, 900); delay(40);
        noTone(buzzerPin);
    }

    void catchMyself() {
        // "Oh!" — quick ascending surprise
        tone(buzzerPin, 600); delay(40);
        tone(buzzerPin, 900); delay(60);
        noTone(buzzerPin);
    }
}

#endif // DROID_SPEAK_H
