// BodySchema.h
// Spatial self-awareness and intentional movement
// CRITICAL: Transforms Buddy from "servo controller" to "embodied agent"

#ifndef BODY_SCHEMA_H
#define BODY_SCHEMA_H

#include <Arduino.h>

// Physical robot dimensions (adjust to match your actual robot)
struct RobotGeometry {
  float baseHeight;        // Height of base servo axis (cm)
  float armLength;         // Length of arm from nod servo to "eye" point (cm)
  float headOffset;        // Forward offset of head from arm axis (cm)
  
  // Servo zero positions (where forward is 0,0,0 in robot space)
  int baseZero;            // Base servo angle for forward (typically 90°)
  int nodZero;             // Nod servo angle for horizontal (typically 110°)
  int tiltZero;            // Tilt servo for neutral head (typically 85°)
  
  RobotGeometry() {
    // Default values - adjust for your robot
    baseHeight = 8.0;      // cm from table to base servo
    armLength = 12.0;      // cm arm length
    headOffset = 3.0;      // cm head extends forward
    
    baseZero = 90;
    nodZero = 110;
    tiltZero = 85;
  }
};

// Spatial position in robot-centered coordinates
struct SpatialPoint {
  float x, y, z;  // cm from robot center
  
  SpatialPoint() : x(0), y(0), z(0) {}
  SpatialPoint(float _x, float _y, float _z) : x(_x), y(_y), z(_z) {}
  
  float distance() {
    return sqrt(x*x + y*y + z*z);
  }
  
  void print() {
    Serial.print("(");
    Serial.print(x, 1);
    Serial.print(", ");
    Serial.print(y, 1);
    Serial.print(", ");
    Serial.print(z, 1);
    Serial.print(")");
  }
};

// Servo angles
struct ServoAngles {
  int base;   // 10-170°
  int nod;    // 80-150°
  int tilt;   // 20-150°
  
  ServoAngles() : base(90), nod(110), tilt(85) {}
  ServoAngles(int b, int n, int t) : base(b), nod(n), tilt(t) {}
  
  void clamp() {
    base = constrain(base, 10, 170);
    nod = constrain(nod, 80, 150);
    tilt = constrain(tilt, 20, 150);
  }
  
  void print() {
    Serial.print("Base:");
    Serial.print(base);
    Serial.print("° Nod:");
    Serial.print(nod);
    Serial.print("° Tilt:");
    Serial.print(tilt);
    Serial.print("°");
  }
};

class BodySchema {
private:
  RobotGeometry geometry;
  
  // Current state
  ServoAngles currentAngles;
  SpatialPoint currentLookTarget;
  bool isReachable;
  
  // Attention tracking
  SpatialPoint attentionTarget;
  float attentionStrength;
  unsigned long lastAttentionShift;
  
public:
  BodySchema() {
    currentAngles = ServoAngles(90, 110, 85);
    attentionStrength = 0.0;
    lastAttentionShift = 0;
    isReachable = true;
  }
  
  // ============================================
  // FORWARD KINEMATICS (angles → space)
  // ============================================
  
  SpatialPoint forwardKinematics(ServoAngles angles) {
    // Convert servo angles to spatial position of "eye point"
    
    // Base rotation (horizontal sweep)
    float baseRadians = (angles.base - geometry.baseZero) * DEG_TO_RAD;
    
    // Nod angle (vertical tilt of arm)
    float nodRadians = (angles.nod - geometry.nodZero) * DEG_TO_RAD;
    
    // Calculate end point of arm in 3D space
    float armProjection = geometry.armLength * cos(nodRadians);
    float armHeight = geometry.armLength * sin(nodRadians);
    
    // Add head offset (extends forward from arm)
    float totalReach = armProjection + geometry.headOffset;
    
    // Convert to Cartesian coordinates
    SpatialPoint point;
    point.x = totalReach * sin(baseRadians);  // Left/Right
    point.y = totalReach * cos(baseRadians);  // Forward/Back
    point.z = geometry.baseHeight + armHeight; // Height
    
    return point;
  }
  
  SpatialPoint getCurrentLookPoint() {
    return forwardKinematics(currentAngles);
  }
  
  // ============================================
  // INVERSE KINEMATICS (space → angles)
  // ============================================
  
  ServoAngles inverseKinematics(SpatialPoint target, bool& reachable) {
    ServoAngles result;
    reachable = true;
    
    // Distance from robot center to target (horizontal)
    float horizontalDist = sqrt(target.x * target.x + target.y * target.y);
    
    // Height difference from base
    float heightDiff = target.z - geometry.baseHeight;
    
    // === BASE SERVO (horizontal rotation) ===
    float baseRadians = atan2(target.x, target.y);  // atan2(left/right, forward/back)
    result.base = geometry.baseZero + (int)(baseRadians * RAD_TO_DEG);
    
    // === NOD SERVO (vertical angle) ===
    // Account for head offset extending forward
    float effectiveReach = horizontalDist - geometry.headOffset;
    
    if (effectiveReach < 0) {
      effectiveReach = 0;  // Too close, look down
    }
    
    // Distance from nod pivot to target
    float distFromPivot = sqrt(effectiveReach * effectiveReach + heightDiff * heightDiff);
    
    // Check if target is reachable
    if (distFromPivot > geometry.armLength * 1.2) {
      reachable = false;
      // Point in general direction at max reach
      distFromPivot = geometry.armLength;
    }
    
    // Calculate nod angle
    float nodRadians = atan2(heightDiff, effectiveReach);
    result.nod = geometry.nodZero + (int)(nodRadians * RAD_TO_DEG);
    
    // === TILT SERVO (head tilt) ===
    // Keep neutral for now (could add expressiveness later)
    result.tilt = geometry.tiltZero;
    
    // Clamp to safe ranges
    result.clamp();
    
    return result;
  }
  
  // ============================================
  // HIGH-LEVEL SPATIAL COMMANDS
  // ============================================
  
  ServoAngles lookAt(float x, float y, float z) {
    SpatialPoint target(x, y, z);

    bool reachable;
    ServoAngles angles = inverseKinematics(target, reachable);

    currentAngles = angles;
    currentLookTarget = target;
    isReachable = reachable;

    return angles;
  }
  
  ServoAngles lookAtDirection(int direction, float distance = 50.0) {
    // Convert 8-directional bin to spatial coordinates
    // direction: 0=front, 1=front-right, 2=right, etc.

    float angle = direction * 45.0;  // Degrees
    float angleRad = angle * DEG_TO_RAD;

    float x = distance * sin(angleRad);
    float y = distance * cos(angleRad);
    float z = geometry.baseHeight + 10.0;  // Roughly eye height

    return lookAt(x, y, z);
  }
  
  ServoAngles lookAtDistance(float distance, int baseAngle = 90, int heightOffset = 0) {
    // Look at a point at specific distance and base angle
    
    float baseRad = (baseAngle - geometry.baseZero) * DEG_TO_RAD;
    
    float x = distance * sin(baseRad);
    float y = distance * cos(baseRad);
    float z = geometry.baseHeight + heightOffset;
    
    return lookAt(x, y, z);
  }
  
  // ============================================
  // ATTENTION SYSTEM INTEGRATION
  // ============================================
  
  void setAttentionTarget(SpatialPoint target, float strength = 1.0) {
    attentionTarget = target;
    attentionStrength = strength;
    lastAttentionShift = millis();
    
    Serial.print("[ATTENTION] New target: ");
    target.print();
    Serial.print(" (strength: ");
    Serial.print(strength, 2);
    Serial.println(")");
  }
  
  void setAttentionDirection(int direction, float distance, float strength = 1.0) {
    float angle = direction * 45.0 * DEG_TO_RAD;
    
    SpatialPoint target;
    target.x = distance * sin(angle);
    target.y = distance * cos(angle);
    target.z = geometry.baseHeight + 10.0;
    
    setAttentionTarget(target, strength);
  }
  
  ServoAngles trackAttention(float smoothness = 0.3) {
    // Smoothly move toward attention target
    
    if (attentionStrength < 0.1) {
      return currentAngles;  // No attention target
    }
    
    // Interpolate between current and target
    SpatialPoint current = getCurrentLookPoint();
    
    float t = smoothness * attentionStrength;
    
    SpatialPoint intermediate;
    intermediate.x = current.x + (attentionTarget.x - current.x) * t;
    intermediate.y = current.y + (attentionTarget.y - current.y) * t;
    intermediate.z = current.z + (attentionTarget.z - current.z) * t;
    
    return lookAt(intermediate.x, intermediate.y, intermediate.z);
  }
  
  void clearAttention() {
    attentionStrength = 0.0;
  }
  
  float getAttentionStrength() {
    return attentionStrength;
  }
  
  // ============================================
  // SPATIAL SCANNING PATTERNS
  // ============================================
  
  void generateScanPattern(SpatialPoint points[], int& count, int maxPoints,
                          float minDist = 30.0, float maxDist = 80.0) {
    // Generate natural scanning pattern in 3D space
    count = 0;
    
    // Center forward
    points[count++] = SpatialPoint(0, 50, geometry.baseHeight + 15);
    
    // Left side
    points[count++] = SpatialPoint(-40, 50, geometry.baseHeight + 10);
    points[count++] = SpatialPoint(-60, 40, geometry.baseHeight + 15);
    
    // Right side
    points[count++] = SpatialPoint(40, 50, geometry.baseHeight + 10);
    points[count++] = SpatialPoint(60, 40, geometry.baseHeight + 15);
    
    // High center
    points[count++] = SpatialPoint(0, 45, geometry.baseHeight + 25);
    
    // Low sides
    points[count++] = SpatialPoint(-30, 50, geometry.baseHeight + 5);
    points[count++] = SpatialPoint(30, 50, geometry.baseHeight + 5);
    
    count = constrain(count, 0, maxPoints);
  }
  
  ServoAngles exploreRandomly(float minDist = 30.0, float maxDist = 80.0) {
    // Generate random exploration target in reachable space
    
    float angle = random(0, 360) * DEG_TO_RAD;
    float distance = random((int)minDist, (int)maxDist);
    float height = geometry.baseHeight + random(-5, 20);
    
    float x = distance * sin(angle);
    float y = distance * cos(angle);
    float z = height;
    
    Serial.print("[EXPLORE] Random target: ");
    Serial.print(distance, 0);
    Serial.print("cm at ");
    Serial.print(angle * RAD_TO_DEG, 0);
    Serial.println("°");
    
    return lookAt(x, y, z);
  }
  
  // ============================================
  // PROPRIOCEPTION (self-awareness)
  // ============================================
  
  void updateCurrentAngles(int base, int nod, int tilt) {
    currentAngles = ServoAngles(base, nod, tilt);
  }
  
  ServoAngles getCurrentAngles() {
    return currentAngles;
  }
  
  bool isCurrentlyReachable() {
    return isReachable;
  }
  
  float getDistanceToTarget() {
    SpatialPoint current = getCurrentLookPoint();
    
    float dx = currentLookTarget.x - current.x;
    float dy = currentLookTarget.y - current.y;
    float dz = currentLookTarget.z - current.z;
    
    return sqrt(dx*dx + dy*dy + dz*dz);
  }
  
  // ============================================
  // GEOMETRY CALIBRATION
  // ============================================
  
  void setGeometry(float baseH, float armLen, float headOff) {
    geometry.baseHeight = baseH;
    geometry.armLength = armLen;
    geometry.headOffset = headOff;
  }
  
  void setZeroPositions(int base, int nod, int tilt) {
    geometry.baseZero = base;
    geometry.nodZero = nod;
    geometry.tiltZero = tilt;
  }
  
  // ============================================
  // DIAGNOSTICS
  // ============================================
  
  void print() {
    Serial.println("--- BODY SCHEMA ---");
    
    Serial.print("  Current angles: ");
    currentAngles.print();
    Serial.println();
    
    SpatialPoint lookPoint = getCurrentLookPoint();
    Serial.print("  Looking at: ");
    lookPoint.print();
    Serial.println();
    
    Serial.print("  Distance: ");
    Serial.print(lookPoint.distance(), 1);
    Serial.println(" cm");
    
    if (attentionStrength > 0.1) {
      Serial.print("  Attention target: ");
      attentionTarget.print();
      Serial.print(" (strength: ");
      Serial.print(attentionStrength, 2);
      Serial.println(")");
    }
    
    Serial.print("  Target reachable: ");
    Serial.println(isReachable ? "YES" : "NO");
  }
  
  void printCompact() {
    Serial.print("  [BODY] Looking ");
    getCurrentLookPoint().print();
    Serial.print(" @ ");
    Serial.print(getCurrentLookPoint().distance(), 0);
    Serial.print("cm");
    
    if (attentionStrength > 0.3) {
      Serial.print(" | ATT:");
      Serial.print(attentionStrength, 1);
    }
    Serial.println();
  }
  
  void testKinematics() {
    Serial.println("\n╔═══════════════════════════════════╗");
    Serial.println("║  BODY SCHEMA KINEMATICS TEST      ║");
    Serial.println("╚═══════════════════════════════════╝\n");
    
    // Test forward kinematics
    Serial.println("=== FORWARD KINEMATICS TEST ===");
    ServoAngles testAngles[] = {
      ServoAngles(90, 110, 85),   // Center
      ServoAngles(45, 110, 85),   // Left
      ServoAngles(135, 110, 85),  // Right
      ServoAngles(90, 90, 85),    // Down
      ServoAngles(90, 130, 85)    // Up
    };
    
    const char* labels[] = {"Center", "Left", "Right", "Down", "Up"};
    
    for (int i = 0; i < 5; i++) {
      Serial.print(labels[i]);
      Serial.print(": ");
      testAngles[i].print();
      Serial.print(" → ");
      SpatialPoint p = forwardKinematics(testAngles[i]);
      p.print();
      Serial.println();
    }
    
    // Test inverse kinematics
    Serial.println("\n=== INVERSE KINEMATICS TEST ===");
    SpatialPoint testPoints[] = {
      SpatialPoint(0, 50, 20),     // Forward
      SpatialPoint(-30, 40, 18),   // Front-left
      SpatialPoint(30, 40, 18),    // Front-right
      SpatialPoint(0, 30, 10),     // Close low
      SpatialPoint(0, 60, 25)      // Far high
    };
    
    const char* pointLabels[] = {"Forward", "Front-Left", "Front-Right", "Close-Low", "Far-High"};
    
    for (int i = 0; i < 5; i++) {
      Serial.print(pointLabels[i]);
      Serial.print(": ");
      testPoints[i].print();
      Serial.print(" → ");
      
      bool reachable;
      ServoAngles angles = inverseKinematics(testPoints[i], reachable);
      angles.print();
      Serial.print(reachable ? " ✓" : " ⚠");
      Serial.println();
    }
    
    // Test round-trip accuracy
    Serial.println("\n=== ROUND-TRIP ACCURACY TEST ===");
    for (int i = 0; i < 3; i++) {
      Serial.print("Target: ");
      testPoints[i].print();
      
      bool reachable;
      ServoAngles angles = inverseKinematics(testPoints[i], reachable);
      SpatialPoint result = forwardKinematics(angles);
      
      Serial.print(" → Result: ");
      result.print();
      
      float error = sqrt(
        pow(testPoints[i].x - result.x, 2) +
        pow(testPoints[i].y - result.y, 2) +
        pow(testPoints[i].z - result.z, 2)
      );
      
      Serial.print(" | Error: ");
      Serial.print(error, 2);
      Serial.println(" cm");
    }
    
    Serial.println("\n✓ Kinematics test complete\n");
  }
};

#endif // BODY_SCHEMA_H
