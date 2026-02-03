// SpatialMemory.h
// 8-direction spatial awareness with novelty detection, change tracking, and face tracking
// Package 3: Vision Integration added

#ifndef SPATIAL_MEMORY_H
#define SPATIAL_MEMORY_H

#include "Personality.h"

struct SpatialBin {
  float averageDistance;      // Mean distance in this direction
  float variance;             // How dynamic this area is
  float recentChange;         // Recent change magnitude
  int changeFrequency;        // How often this area changes
  float noveltyScore;         // How interesting/novel this direction is
  unsigned long lastUpdate;   // Timestamp of last reading
  int readingCount;           // Number of readings taken
};

class SpatialMemory {
private:
  // 8 directional bins (0=front, 1=front-right, 2=right, etc.)
  SpatialBin bins[8];
  
  // History for temporal patterns
  float recentDistances[8][5];  // Last 5 readings per direction
  int historyIndex[8];
  
  // Detection thresholds
  const float HUMAN_DISTANCE_MIN = 30.0;
  const float HUMAN_DISTANCE_MAX = 150.0;
  const float CHANGE_THRESHOLD = 20.0;
  
public:
  SpatialMemory() {
    for (int i = 0; i < 8; i++) {
      bins[i].averageDistance = 200.0;  // Assume far initially
      bins[i].variance = 0.0;
      bins[i].recentChange = 0.0;
      bins[i].changeFrequency = 0;
      bins[i].noveltyScore = 0.5;
      bins[i].lastUpdate = 0;
      bins[i].readingCount = 0;
      
      historyIndex[i] = 0;
      for (int j = 0; j < 5; j++) {
        recentDistances[i][j] = 200.0;
      }
    }
  }
  
  // ============================================
  // UPDATE
  // ============================================
  
  void updateReading(int direction, float distance) {
    if (direction < 0 || direction >= 8) return;
    
    SpatialBin& bin = bins[direction];
    
    // Calculate change from previous average
    float change = abs(distance - bin.averageDistance);
    bin.recentChange = change;
    
    if (change > CHANGE_THRESHOLD) {
      bin.changeFrequency++;
      bin.noveltyScore = min(1.0f, bin.noveltyScore + 0.1f);
    }
    
    // Update history
    int idx = historyIndex[direction];
    recentDistances[direction][idx] = distance;
    historyIndex[direction] = (idx + 1) % 5;
    
    // Calculate running average
    if (bin.readingCount < 10) {
      // Quick adaptation for first few readings
      bin.averageDistance = bin.averageDistance * 0.7 + distance * 0.3;
    } else {
      // Slower adaptation once stable
      bin.averageDistance = bin.averageDistance * 0.95 + distance * 0.05;
    }
    
    // Calculate variance from recent history
    float sum = 0;
    float mean = 0;
    for (int i = 0; i < 5; i++) {
      mean += recentDistances[direction][i];
    }
    mean /= 5.0;
    
    for (int i = 0; i < 5; i++) {
      float diff = recentDistances[direction][i] - mean;
      sum += diff * diff;
    }
    bin.variance = sqrt(sum / 5.0);
    
    // Novelty decays over time
    unsigned long now = millis();
    if (bin.lastUpdate > 0) {
      float timeSinceUpdate = (now - bin.lastUpdate) / 1000.0;
      bin.noveltyScore *= exp(-0.1 * timeSinceUpdate);  // Exponential decay
    }
    
    bin.lastUpdate = now;
    bin.readingCount++;
  }
  
  // ============================================
  // QUERIES
  // ============================================
  
  float getNovelty(int direction) {
    if (direction < 0 || direction >= 8) return 0.0;
    return bins[direction].noveltyScore;
  }
  
  float getVariance(int direction) {
    if (direction < 0 || direction >= 8) return 0.0;
    return bins[direction].variance;
  }
  
  float getRecentChange(int direction) {
    if (direction < 0 || direction >= 8) return 0.0;
    return bins[direction].recentChange;
  }
  
  float getAverageDistance(int direction) {
    if (direction < 0 || direction >= 8) return 200.0;
    return bins[direction].averageDistance;
  }
  
  // ============================================
  // ANALYSIS
  // ============================================
  
  float getAverageDynamism() {
    // How dynamic is the overall environment?
    float totalVariance = 0;
    int validBins = 0;
    
    for (int i = 0; i < 8; i++) {
      if (bins[i].readingCount > 0) {
        totalVariance += bins[i].variance;
        validBins++;
      }
    }
    
    return validBins > 0 ? (totalVariance / validBins) / 50.0 : 0.0;  // Normalize
  }
  
  float getTotalNovelty() {
    float total = 0;
    int validBins = 0;
    
    for (int i = 0; i < 8; i++) {
      if (bins[i].readingCount > 0) {
        total += bins[i].noveltyScore;
        validBins++;
      }
    }
    
    return validBins > 0 ? total / validBins : 0.0;
  }
  
  float getMaxRecentChange() {
    float maxChange = 0;
    for (int i = 0; i < 8; i++) {
      if (bins[i].recentChange > maxChange) {
        maxChange = bins[i].recentChange;
      }
    }
    return maxChange;
  }
  
  int getMostInterestingDirection(Personality& personality) {
    // Find direction with highest interest score
    float bestScore = -1;
    int bestDirection = 4;  // Default to front
    
    for (int i = 0; i < 8; i++) {
      if (bins[i].readingCount == 0) continue;
      
      // Interest = novelty + variance, weighted by personality
      float interest = bins[i].noveltyScore * personality.getCuriosity() +
                       (bins[i].variance / 50.0) * personality.getExcitability();
      
      if (interest > bestScore) {
        bestScore = interest;
        bestDirection = i;
      }
    }
    
    return bestDirection;
  }
  
  bool likelyHumanPresent() {
    // Heuristic: sustained presence in human distance range with low variance
    for (int i = 0; i < 8; i++) {
      if (bins[i].averageDistance >= HUMAN_DISTANCE_MIN &&
          bins[i].averageDistance <= HUMAN_DISTANCE_MAX &&
          bins[i].variance < 30.0 &&
          bins[i].readingCount > 3) {
        return true;
      }
    }
    return false;
  }
  
  // ============================================
  // FACE/PERSON TRACKING (PACKAGE 3)
  // ============================================
  
  void recordFaceAt(int direction, float distance) {
    if (direction < 0 || direction >= 8) return;
    
    SpatialBin& bin = bins[direction];
    
    // Update distance in this direction
    updateReading(direction, distance);
    
    // Boost novelty score (face = interesting!)
    bin.noveltyScore = min(1.0f, bin.noveltyScore + 0.2f);
    
    // Reduce variance assumption (faces are stable)
    bin.variance = max(0.0f, bin.variance - 5.0f);

    // ═══════════════════════════════════════════════════════════════
    // PERFORMANCE: Serial spam removed - was printing 20-50 times/sec
    // Each Serial.print() blocks for 1-2ms, causing cumulative lag
    // Spatial memory still works correctly, just silently
    // ═══════════════════════════════════════════════════════════════
    // REMOVED: [SPATIAL] Face recorded debug spam
  }
  
  bool hasFaceInDirection(int direction) {
    if (direction < 0 || direction >= 8) return false;
    
    SpatialBin& bin = bins[direction];
    
    // Consider "has face" if:
    // 1. Recently updated (within 3 seconds)
    // 2. In human distance range
    // 3. Low variance (stable presence)
    
    unsigned long now = millis();
    unsigned long age = (bin.lastUpdate > 0) ? (now - bin.lastUpdate) : 9999;
    
    bool recentlyUpdated = (age < 3000);
    bool inHumanRange = (bin.averageDistance >= HUMAN_DISTANCE_MIN && 
                         bin.averageDistance <= HUMAN_DISTANCE_MAX);
    bool stablePresence = (bin.variance < 25.0);
    
    return (recentlyUpdated && inHumanRange && stablePresence);
  }
  
  float getFaceDistance(int direction) {
    if (direction < 0 || direction >= 8) return 999.0;
    return bins[direction].averageDistance;
  }
  
  int getClosestFaceDirection() {
    // Find direction with closest face
    float closestDist = 999.0;
    int closestDir = 0;  // Default forward
    
    for (int i = 0; i < 8; i++) {
      if (hasFaceInDirection(i)) {
        float dist = bins[i].averageDistance;
        if (dist < closestDist) {
          closestDist = dist;
          closestDir = i;
        }
      }
    }
    
    return closestDir;
  }
  
  int countVisibleFaces() {
    // How many directions have faces?
    int count = 0;
    for (int i = 0; i < 8; i++) {
      if (hasFaceInDirection(i)) {
        count++;
      }
    }
    return count;
  }
  
  // ============================================
  // DIAGNOSTICS
  // ============================================
  
  void print() {
    Serial.println("--- SPATIAL MEMORY (8 directions) ---");
    const char* dirNames[] = {"Front", "Front-R", "Right", "Back-R", 
                              "Back", "Back-L", "Left", "Front-L"};
    
    for (int i = 0; i < 8; i++) {
      if (bins[i].readingCount == 0) continue;
      
      Serial.print("  ");
      Serial.print(dirNames[i]);
      Serial.print(": ");
      Serial.print(bins[i].averageDistance, 0);
      Serial.print("cm (var:");
      Serial.print(bins[i].variance, 1);
      Serial.print(" nov:");
      Serial.print(bins[i].noveltyScore, 2);
      Serial.print(" chg:");
      Serial.print(bins[i].recentChange, 0);
      Serial.print(" n=");
      Serial.print(bins[i].readingCount);
      
      // NEW: Indicate if face detected in this direction
      if (hasFaceInDirection(i)) {
        Serial.print(" FACE");
      }
      
      Serial.println(")");
    }
    
    Serial.print("  Overall dynamism: ");
    Serial.println(getAverageDynamism(), 2);
    Serial.print("  Total novelty: ");
    Serial.println(getTotalNovelty(), 2);
    Serial.print("  Human likely present: ");
    Serial.println(likelyHumanPresent() ? "YES" : "NO");
    
    // NEW: Face tracking diagnostics
    int faceCount = countVisibleFaces();
    if (faceCount > 0) {
      Serial.print("  Faces detected: ");
      Serial.print(faceCount);
      Serial.print(" in direction(s): ");
      for (int i = 0; i < 8; i++) {
        if (hasFaceInDirection(i)) {
          Serial.print(i);
          Serial.print(" ");
        }
      }
      Serial.println();
      
      int closestDir = getClosestFaceDirection();
      Serial.print("  Closest face: ");
      Serial.print(dirNames[closestDir]);
      Serial.print(" at ");
      Serial.print(getFaceDistance(closestDir), 0);
      Serial.println("cm");
    }
  }
  
  void printCompact() {
    Serial.print("  [MEMORY] Dyn:");
    Serial.print(getAverageDynamism(), 2);
    Serial.print(" Nov:");
    Serial.print(getTotalNovelty(), 2);
    Serial.print(" Human:");
    Serial.print(likelyHumanPresent() ? "Y" : "N");
    
    // NEW: Add face count to compact view
    int faceCount = countVisibleFaces();
    if (faceCount > 0) {
      Serial.print(" Faces:");
      Serial.print(faceCount);
    }
    
    Serial.println();
  }
};

#endif // SPATIAL_MEMORY_H