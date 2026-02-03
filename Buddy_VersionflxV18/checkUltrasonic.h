//This function pings the Ultrasonic Sensor and returns a distance in CM

int checkUltra(int theEchoPin, int theTrigPin) {
  long duration, distance;
  
  // Trigger ultrasonic pulse
  digitalWrite(theTrigPin, LOW);
  delayMicroseconds(2);

  digitalWrite(theTrigPin, HIGH);
  delayMicroseconds(10);

  digitalWrite(theTrigPin, LOW);
  duration = pulseIn(theEchoPin, HIGH, 30000); // 30ms timeout

  // Calculate distance
  distance = duration / 58.2;
  
  // Validate reading
  if (distance == 0 || distance > 400) {
    Serial.print("⚠ Sensor warning: ");
    if (distance == 0) {
      Serial.println("No echo received (timeout)");
    } else {
      Serial.println("Reading out of range");
    }
    distance = 400; // Default to max range
  }
  
  return distance;
}
