//This Function Generates a Random series of beeps to create speech

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
