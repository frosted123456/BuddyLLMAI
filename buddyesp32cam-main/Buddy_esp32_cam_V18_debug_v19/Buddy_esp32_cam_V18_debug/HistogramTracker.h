/**
 * HistogramTracker v5.0 - ACCURATE BRIDGING
 * 
 * DESIGN GOALS:
 *   1. Bridge AI gaps reliably (fast movement, face turning, blur)
 *   2. Coordinate accuracy matching AI detection (~1-2 pixels)
 *   3. Fail fast when face ACTUALLY gone (not just AI struggling)
 *   4. Never lock onto wrong target indefinitely
 * 
 * KEY FEATURES:
 *   - Two-stage search: coarse (4px) → fine (1px) for accuracy
 *   - Quality-based adaptive timeout (good tracking = longer bridge)
 *   - Confidence trend monitoring (dropping = suspicious)
 *   - Position stability check (jumping around = wrong target)
 *   - Skin collapse = immediate failure (face actually gone)
 * 
 * PHILOSOPHY: Trust histogram based on HOW WELL it's tracking,
 *             not just HOW LONG since AI confirmed.
 */

#ifndef HISTOGRAM_TRACKER_H
#define HISTOGRAM_TRACKER_H

#include "esp_camera.h"
#include <Arduino.h>

// ============================================
// TIMING CONFIGURATION
// ============================================
#define SIGNATURE_MAX_AGE_MS 1200       // Absolute max (good tracking can go this long)
#define SIGNATURE_MIN_AGE_MS 400        // Minimum before quality checks kick in
#define MAX_HISTOGRAM_FRAMES_GOOD 12    // Max frames if tracking quality is GOOD
#define MAX_HISTOGRAM_FRAMES_POOR 5     // Max frames if tracking quality is POOR

// ============================================
// SEARCH CONFIGURATION (ACCURACY)
// ============================================
#define HIST_BINS 16
#define BASE_SEARCH_RADIUS 50
#define COARSE_STEP 4                   // Coarse search grid
#define FINE_STEP 1                     // Fine refinement grid  
#define FINE_RADIUS 6                   // Fine search radius around coarse best

// ============================================
// CONFIDENCE THRESHOLDS
// ============================================
#define CONFIDENCE_THRESHOLD 0.52       // Base threshold
#define CONFIDENCE_HIGH 0.70            // "Good tracking" threshold
#define CONFIDENCE_DROP_ALERT 0.08      // Alert if confidence drops this much

// ============================================
// REGION MATCHING
// ============================================
#define MIN_REGION_CONFIDENCE 0.45
#define MIN_REGIONS_PASSING 2           // 2-of-3 regions

// ============================================
// SKIN DETECTION (PRIMARY EXIT CONDITION)
// ============================================
#define MIN_SKIN_PERCENTAGE 0.20        // Minimum for candidate
#define SKIN_COLLAPSE_THRESHOLD 0.10    // Below = face definitely gone
#define SIGNATURE_SKIN_RATIO_MIN 0.28   // Signature quality requirement

// ============================================
// QUALITY MONITORING
// ============================================
#define QUALITY_HISTORY_SIZE 5          // Frames to track for trends
#define MAX_POSITION_JUMP 25            // Suspicious if position jumps more than this
#define POSITION_STABILITY_THRESHOLD 3  // Consecutive stable frames for "good" status

// ============================================
// COLOR DRIFT (SOFT PENALTY)
// ============================================
#define MEAN_HUE_DRIFT_SOFT 12
#define MEAN_HUE_DRIFT_HARD 30
#define MEAN_SAT_DRIFT_SOFT 20
#define MEAN_SAT_DRIFT_HARD 50
#define MEAN_VAL_DRIFT_SOFT 25
#define MEAN_VAL_DRIFT_HARD 60

// ============================================
// OTHER CHECKS
// ============================================
#define MIN_COHERENCE_SCORE 0.42
#define MIN_TEXTURE_VARIANCE 60
#define MAX_TEXTURE_VARIANCE 3000
#define MATCH_DISTANCE_LIMIT 60

// ============================================
// ADAPTIVE SEARCH RADIUS
// ============================================
#define SPEED_THRESHOLD_FAST 30.0
#define SPEED_THRESHOLD_SLOW 10.0
#define SEARCH_RADIUS_FAST 90
#define SEARCH_RADIUS_SLOW 45

// ============================================
// SKIN TONE HSV RANGES
// ============================================
#define SKIN_H_MIN 0
#define SKIN_H_MAX 25
#define SKIN_S_MIN 25
#define SKIN_S_MAX 95
#define SKIN_V_MIN 45
#define SKIN_V_MAX 98

// ============================================
// TRACKING QUALITY ENUM
// ============================================
enum TrackingQuality {
    QUALITY_GOOD,       // High confidence, stable position
    QUALITY_MODERATE,   // Acceptable confidence, some variation
    QUALITY_POOR        // Low/dropping confidence, unstable
};

class HistogramTracker {
private:
    // Multi-region histograms
    float signatureHistTop[HIST_BINS];
    float signatureHistMid[HIST_BINS];
    float signatureHistBot[HIST_BINS];
    float signatureSatHistTop[HIST_BINS];
    float signatureSatHistMid[HIST_BINS];
    float signatureSatHistBot[HIST_BINS];
    
    // Signature properties
    float meanHue, meanSat, meanVal;
    float textureVariance;
    float signatureSkinRatio;
    int signaturePixelCount;
    bool signatureValid;
    
    // Position tracking
    int centerX, centerY;
    int searchRadius;
    
    // Timing
    unsigned long signatureTime;
    int histogramOnlyFrames;
    
    // Quality monitoring (rolling history)
    float confidenceHistory[QUALITY_HISTORY_SIZE];
    int positionHistoryX[QUALITY_HISTORY_SIZE];
    int positionHistoryY[QUALITY_HISTORY_SIZE];
    int historyIndex;
    int historyCount;
    
    // Stability tracking
    int consecutiveStableFrames;
    int consecutiveCollapses;
    int lastMatchX, lastMatchY;
    float lastConfidence;
    
    // ══════════════════════════════════════════════════════════════
    // HELPER FUNCTIONS
    // ══════════════════════════════════════════════════════════════
    
    void rgb565ToHSV(uint16_t rgb565, int& h, int& s, int& v) {
        int r = ((rgb565 >> 11) & 0x1F) << 3;
        int g = ((rgb565 >> 5) & 0x3F) << 2;
        int b = (rgb565 & 0x1F) << 3;
        
        int maxRGB = max(r, max(g, b));
        int minRGB = min(r, min(g, b));
        int delta = maxRGB - minRGB;
        
        v = (maxRGB * 100) / 255;
        s = (maxRGB == 0) ? 0 : (delta * 100) / maxRGB;
        
        if (delta == 0) {
            h = 0;
        } else if (maxRGB == r) {
            h = 30 * ((g - b) / delta);
            if (h < 0) h += 180;
        } else if (maxRGB == g) {
            h = 30 * (2 + (b - r) / delta);
        } else {
            h = 30 * (4 + (r - g) / delta);
        }
        
        h = constrain(h, 0, 179);
        s = constrain(s, 0, 100);
        v = constrain(v, 0, 100);
    }
    
    void getPixelHSV(uint8_t* frame, int x, int y, int& h, int& s, int& v) {
        if (x < 0 || x >= 240 || y < 0 || y >= 240) {
            h = s = v = 0;
            return;
        }
        int idx = (y * 240 + x) * 2;
        uint16_t pixel = (frame[idx + 1] << 8) | frame[idx];
        rgb565ToHSV(pixel, h, s, v);
    }
    
    bool isSkinTone(int h, int s, int v) {
        return (h >= SKIN_H_MIN && h <= SKIN_H_MAX &&
                s >= SKIN_S_MIN && s <= SKIN_S_MAX &&
                v >= SKIN_V_MIN && v <= SKIN_V_MAX);
    }
    
    float bhattacharyyaCoefficient(float* hist1, float* hist2, int bins) {
        float sum = 0.0;
        for (int i = 0; i < bins; i++) {
            sum += sqrt(hist1[i] * hist2[i]);
        }
        return sum;
    }
    
    float calculateCoherence(uint8_t* frame, int cx, int cy, int radius) {
        const int gridSize = 8;
        int cellCounts[8][8] = {0};
        int totalPixels = 0;
        
        int x1 = max(0, cx - radius);
        int y1 = max(0, cy - radius);
        int x2 = min(240, cx + radius);
        int y2 = min(240, cy + radius);
        
        int cellWidth = (x2 - x1) / gridSize;
        int cellHeight = (y2 - y1) / gridSize;
        if (cellWidth <= 0 || cellHeight <= 0) return 0.0;
        
        for (int y = y1; y < y2; y += 2) {
            for (int x = x1; x < x2; x += 2) {
                int h, s, v;
                getPixelHSV(frame, x, y, h, s, v);
                if (isSkinTone(h, s, v)) {
                    int cellX = min((x - x1) / cellWidth, gridSize - 1);
                    int cellY = min((y - y1) / cellHeight, gridSize - 1);
                    cellCounts[cellY][cellX]++;
                    totalPixels++;
                }
            }
        }
        
        if (totalPixels < 10) return 0.0;
        
        int occupiedCells = 0, connectedClusters = 0;
        for (int y = 0; y < gridSize; y++) {
            for (int x = 0; x < gridSize; x++) {
                if (cellCounts[y][x] > 0) {
                    occupiedCells++;
                    for (int dy = -1; dy <= 1; dy++) {
                        for (int dx = -1; dx <= 1; dx++) {
                            if (dx == 0 && dy == 0) continue;
                            int ny = y + dy, nx = x + dx;
                            if (ny >= 0 && ny < gridSize && nx >= 0 && nx < gridSize) {
                                if (cellCounts[ny][nx] > 0) {
                                    connectedClusters++;
                                    goto nextCell;
                                }
                            }
                        }
                    }
                    nextCell:;
                }
            }
        }
        return occupiedCells > 0 ? (float)connectedClusters / occupiedCells : 0.0;
    }
    
    float calculateTextureVariance(uint8_t* frame, int cx, int cy, int radius) {
        int x1 = max(0, cx - radius);
        int y1 = max(0, cy - radius);
        int x2 = min(240, cx + radius);
        int y2 = min(240, cy + radius);
        
        float sum = 0.0, sumSq = 0.0;
        int count = 0;
        
        for (int y = y1; y < y2; y += 3) {
            for (int x = x1; x < x2; x += 3) {
                int h, s, v;
                getPixelHSV(frame, x, y, h, s, v);
                sum += v;
                sumSq += v * v;
                count++;
            }
        }
        
        if (count < 2) return 0.0;
        float mean = sum / count;
        return (sumSq / count) - (mean * mean);
    }
    
    float calculateDriftPenalty(float candMeanH, float candMeanS, float candMeanV) {
        float hueDiff = abs(candMeanH - meanHue);
        float satDiff = abs(candMeanS - meanSat);
        float valDiff = abs(candMeanV - meanVal);
        
        if (hueDiff > MEAN_HUE_DRIFT_HARD ||
            satDiff > MEAN_SAT_DRIFT_HARD ||
            valDiff > MEAN_VAL_DRIFT_HARD) {
            return 1.0;
        }
        
        float huePenalty = (hueDiff > MEAN_HUE_DRIFT_SOFT) ?
            (hueDiff - MEAN_HUE_DRIFT_SOFT) / (float)(MEAN_HUE_DRIFT_HARD - MEAN_HUE_DRIFT_SOFT) : 0.0;
        float satPenalty = (satDiff > MEAN_SAT_DRIFT_SOFT) ?
            (satDiff - MEAN_SAT_DRIFT_SOFT) / (float)(MEAN_SAT_DRIFT_HARD - MEAN_SAT_DRIFT_SOFT) : 0.0;
        float valPenalty = (valDiff > MEAN_VAL_DRIFT_SOFT) ?
            (valDiff - MEAN_VAL_DRIFT_SOFT) / (float)(MEAN_VAL_DRIFT_HARD - MEAN_VAL_DRIFT_SOFT) : 0.0;
        
        return 0.4 * huePenalty + 0.3 * satPenalty + 0.3 * valPenalty;
    }
    
    // ══════════════════════════════════════════════════════════════
    // CANDIDATE EVALUATION (shared by coarse and fine search)
    // ══════════════════════════════════════════════════════════════
    
    struct CandidateResult {
        float confidence;
        float skinPercentage;
        bool valid;
    };
    
    CandidateResult evaluateCandidate(uint8_t* frame, int x, int y) {
        CandidateResult result = {0.0, 0.0, false};
        
        float candHistTop[HIST_BINS] = {0}, candHistMid[HIST_BINS] = {0}, candHistBot[HIST_BINS] = {0};
        float candSatHistTop[HIST_BINS] = {0}, candSatHistMid[HIST_BINS] = {0}, candSatHistBot[HIST_BINS] = {0};
        
        float weightTop = 0.0, weightMid = 0.0, weightBot = 0.0;
        float sumH = 0.0, sumS = 0.0, sumV = 0.0;
        int totalPixels = 0, skinPixels = 0;
        
        int rx1 = max(0, x - 15), ry1 = max(0, y - 15);
        int rx2 = min(240, x + 15), ry2 = min(240, y + 15);
        int regionHeight = (ry2 - ry1) / 3;
        int topY2 = ry1 + regionHeight;
        int midY2 = topY2 + regionHeight;
        
        for (int cy = ry1; cy < ry2; cy += 2) {
            for (int cx = rx1; cx < rx2; cx += 2) {
                int h, s, v;
                getPixelHSV(frame, cx, cy, h, s, v);
                
                if (isSkinTone(h, s, v)) skinPixels++;
                
                int hBin = min((h * HIST_BINS) / 180, HIST_BINS - 1);
                int sBin = min((s * HIST_BINS) / 100, HIST_BINS - 1);
                
                sumH += h; sumS += s; sumV += v;
                totalPixels++;
                
                if (cy < topY2) {
                    candHistTop[hBin] += 1.0; candSatHistTop[sBin] += 1.0; weightTop += 1.0;
                } else if (cy < midY2) {
                    candHistMid[hBin] += 1.0; candSatHistMid[sBin] += 1.0; weightMid += 1.0;
                } else {
                    candHistBot[hBin] += 1.0; candSatHistBot[sBin] += 1.0; weightBot += 1.0;
                }
            }
        }
        
        if (totalPixels == 0) return result;
        
        result.skinPercentage = (float)skinPixels / totalPixels;
        if (result.skinPercentage < MIN_SKIN_PERCENTAGE) return result;
        
        // Mean color drift
        float candMeanH = sumH / totalPixels;
        float candMeanS = sumS / totalPixels;
        float candMeanV = sumV / totalPixels;
        float driftPenalty = calculateDriftPenalty(candMeanH, candMeanS, candMeanV);
        if (driftPenalty >= 1.0) return result;
        
        // Normalize histograms
        if (weightTop > 0) for (int i = 0; i < HIST_BINS; i++) {
            candHistTop[i] /= weightTop; candSatHistTop[i] /= weightTop;
        }
        if (weightMid > 0) for (int i = 0; i < HIST_BINS; i++) {
            candHistMid[i] /= weightMid; candSatHistMid[i] /= weightMid;
        }
        if (weightBot > 0) for (int i = 0; i < HIST_BINS; i++) {
            candHistBot[i] /= weightBot; candSatHistBot[i] /= weightBot;
        }
        
        // Region confidence
        float topConf = 0.6 * bhattacharyyaCoefficient(signatureHistTop, candHistTop, HIST_BINS) +
                       0.4 * bhattacharyyaCoefficient(signatureSatHistTop, candSatHistTop, HIST_BINS);
        float midConf = 0.6 * bhattacharyyaCoefficient(signatureHistMid, candHistMid, HIST_BINS) +
                       0.4 * bhattacharyyaCoefficient(signatureSatHistMid, candSatHistMid, HIST_BINS);
        float botConf = 0.6 * bhattacharyyaCoefficient(signatureHistBot, candHistBot, HIST_BINS) +
                       0.4 * bhattacharyyaCoefficient(signatureSatHistBot, candSatHistBot, HIST_BINS);
        
        // 2-of-3 regions must pass
        int regionsPassing = 0;
        if (topConf >= MIN_REGION_CONFIDENCE) regionsPassing++;
        if (midConf >= MIN_REGION_CONFIDENCE) regionsPassing++;
        if (botConf >= MIN_REGION_CONFIDENCE) regionsPassing++;
        if (regionsPassing < MIN_REGIONS_PASSING) return result;
        
        float confidence = (topConf + midConf + botConf) / 3.0;
        confidence *= (1.0 - 0.25 * driftPenalty);
        
        result.confidence = confidence;
        result.valid = true;
        return result;
    }
    
    // ══════════════════════════════════════════════════════════════
    // QUALITY ASSESSMENT
    // ══════════════════════════════════════════════════════════════
    
    void updateHistory(float confidence, int x, int y) {
        confidenceHistory[historyIndex] = confidence;
        positionHistoryX[historyIndex] = x;
        positionHistoryY[historyIndex] = y;
        historyIndex = (historyIndex + 1) % QUALITY_HISTORY_SIZE;
        if (historyCount < QUALITY_HISTORY_SIZE) historyCount++;
    }
    
    TrackingQuality assessQuality() {
        if (historyCount < 3) return QUALITY_MODERATE;  // Not enough data
        
        // Calculate confidence trend
        float recentAvg = 0.0, olderAvg = 0.0;
        int recentCount = 0, olderCount = 0;
        
        for (int i = 0; i < historyCount; i++) {
            int idx = (historyIndex - 1 - i + QUALITY_HISTORY_SIZE) % QUALITY_HISTORY_SIZE;
            if (i < 2) {
                recentAvg += confidenceHistory[idx];
                recentCount++;
            } else {
                olderAvg += confidenceHistory[idx];
                olderCount++;
            }
        }
        
        if (recentCount > 0) recentAvg /= recentCount;
        if (olderCount > 0) olderAvg /= olderCount;
        
        bool confidenceDropping = (olderCount > 0 && recentAvg < olderAvg - CONFIDENCE_DROP_ALERT);
        bool confidenceHigh = (recentAvg >= CONFIDENCE_HIGH);
        
        // Calculate position stability
        float maxJump = 0.0;
        for (int i = 1; i < historyCount; i++) {
            int idx1 = (historyIndex - i + QUALITY_HISTORY_SIZE) % QUALITY_HISTORY_SIZE;
            int idx2 = (historyIndex - i - 1 + QUALITY_HISTORY_SIZE) % QUALITY_HISTORY_SIZE;
            float dx = positionHistoryX[idx1] - positionHistoryX[idx2];
            float dy = positionHistoryY[idx1] - positionHistoryY[idx2];
            float jump = sqrt(dx*dx + dy*dy);
            if (jump > maxJump) maxJump = jump;
        }
        
        bool positionStable = (maxJump < MAX_POSITION_JUMP);
        
        // Determine quality
        if (confidenceDropping || !positionStable) {
            return QUALITY_POOR;
        } else if (confidenceHigh && positionStable) {
            return QUALITY_GOOD;
        } else {
            return QUALITY_MODERATE;
        }
    }
    
    int getMaxFramesForQuality(TrackingQuality quality) {
        switch (quality) {
            case QUALITY_GOOD: return MAX_HISTOGRAM_FRAMES_GOOD;
            case QUALITY_MODERATE: return (MAX_HISTOGRAM_FRAMES_GOOD + MAX_HISTOGRAM_FRAMES_POOR) / 2;
            case QUALITY_POOR: return MAX_HISTOGRAM_FRAMES_POOR;
            default: return MAX_HISTOGRAM_FRAMES_POOR;
        }
    }
    
public:
    HistogramTracker() {
        reset();
    }
    
    void reset() {
        centerX = centerY = 120;
        searchRadius = BASE_SEARCH_RADIUS;
        signatureTime = 0;
        histogramOnlyFrames = 0;
        signaturePixelCount = 0;
        signatureValid = false;
        historyIndex = 0;
        historyCount = 0;
        consecutiveStableFrames = 0;
        consecutiveCollapses = 0;
        lastMatchX = lastMatchY = 120;
        lastConfidence = 0.0;
        meanHue = meanSat = meanVal = 0.0;
        textureVariance = signatureSkinRatio = 0.0;
        
        for (int i = 0; i < HIST_BINS; i++) {
            signatureHistTop[i] = signatureHistMid[i] = signatureHistBot[i] = 0.0;
            signatureSatHistTop[i] = signatureSatHistMid[i] = signatureSatHistBot[i] = 0.0;
        }
        for (int i = 0; i < QUALITY_HISTORY_SIZE; i++) {
            confidenceHistory[i] = 0.0;
            positionHistoryX[i] = positionHistoryY[i] = 120;
        }
    }
    
    void buildSignature(uint8_t* frame, int faceX, int faceY, int faceW, int faceH) {
        if (!frame) return;
        
        // Reset histograms
        for (int i = 0; i < HIST_BINS; i++) {
            signatureHistTop[i] = signatureHistMid[i] = signatureHistBot[i] = 0.0;
            signatureSatHistTop[i] = signatureSatHistMid[i] = signatureSatHistBot[i] = 0.0;
        }
        
        signaturePixelCount = 0;
        
        int x1 = max(0, faceX - faceW/2 - 5);
        int y1 = max(0, faceY - faceH/2 - 5);
        int x2 = min(240, faceX + faceW/2 + 5);
        int y2 = min(240, faceY + faceH/2 + 5);
        
        int regionHeight = (y2 - y1) / 3;
        int topY2 = y1 + regionHeight;
        int midY2 = topY2 + regionHeight;
        
        float weightTop = 0.0, weightMid = 0.0, weightBot = 0.0;
        float sumH = 0.0, sumS = 0.0, sumV = 0.0;
        int skinPixels = 0;
        
        for (int y = y1; y < y2; y += 2) {
            for (int x = x1; x < x2; x += 2) {
                int h, s, v;
                getPixelHSV(frame, x, y, h, s, v);
                
                if (isSkinTone(h, s, v)) skinPixels++;
                
                int hBin = min((h * HIST_BINS) / 180, HIST_BINS - 1);
                int sBin = min((s * HIST_BINS) / 100, HIST_BINS - 1);
                
                sumH += h; sumS += s; sumV += v;
                
                if (y < topY2) {
                    signatureHistTop[hBin] += 1.0;
                    signatureSatHistTop[sBin] += 1.0;
                    weightTop += 1.0;
                } else if (y < midY2) {
                    signatureHistMid[hBin] += 1.0;
                    signatureSatHistMid[sBin] += 1.0;
                    weightMid += 1.0;
                } else {
                    signatureHistBot[hBin] += 1.0;
                    signatureSatHistBot[sBin] += 1.0;
                    weightBot += 1.0;
                }
                signaturePixelCount++;
            }
        }
        
        // Normalize
        if (weightTop > 0) for (int i = 0; i < HIST_BINS; i++) {
            signatureHistTop[i] /= weightTop;
            signatureSatHistTop[i] /= weightTop;
        }
        if (weightMid > 0) for (int i = 0; i < HIST_BINS; i++) {
            signatureHistMid[i] /= weightMid;
            signatureSatHistMid[i] /= weightMid;
        }
        if (weightBot > 0) for (int i = 0; i < HIST_BINS; i++) {
            signatureHistBot[i] /= weightBot;
            signatureSatHistBot[i] /= weightBot;
        }
        
        if (signaturePixelCount > 0) {
            meanHue = sumH / signaturePixelCount;
            meanSat = sumS / signaturePixelCount;
            meanVal = sumV / signaturePixelCount;
        }
        
        textureVariance = calculateTextureVariance(frame, faceX, faceY, max(faceW, faceH) / 2);
        signatureSkinRatio = (signaturePixelCount > 0) ? (float)skinPixels / signaturePixelCount : 0.0;
        
        // Reset all tracking state
        centerX = faceX;
        centerY = faceY;
        signatureTime = millis();
        histogramOnlyFrames = 0;
        consecutiveCollapses = 0;
        consecutiveStableFrames = 0;
        lastMatchX = faceX;
        lastMatchY = faceY;
        lastConfidence = 1.0;
        
        // Clear history (fresh start from AI)
        historyIndex = 0;
        historyCount = 0;
        
        signatureValid = (signaturePixelCount > 0 && signatureSkinRatio >= SIGNATURE_SKIN_RATIO_MIN);
    }
    
    bool track(uint8_t* frame, int& outX, int& outY, float& outConfidence,
               int predictedX = 120, int predictedY = 120, float servoSpeed = 0.0f) {
        
        if (!frame || !signatureValid || signaturePixelCount == 0) {
            return false;
        }
        
        unsigned long age = millis() - signatureTime;
        
        // ════════════════════════════════════════════════════════
        // TIMEOUT CHECKS
        // ════════════════════════════════════════════════════════
        
        // Absolute maximum age
        if (age > SIGNATURE_MAX_AGE_MS) {
            signatureValid = false;
            return false;
        }
        
        // Quality-based frame limit (only checked after minimum age)
        if (age > SIGNATURE_MIN_AGE_MS) {
            TrackingQuality quality = assessQuality();
            int maxFrames = getMaxFramesForQuality(quality);
            if (histogramOnlyFrames >= maxFrames) {
                signatureValid = false;
                return false;
            }
        }
        
        // ════════════════════════════════════════════════════════
        // ADAPTIVE SEARCH RADIUS
        // ════════════════════════════════════════════════════════
        
        if (servoSpeed > SPEED_THRESHOLD_FAST) {
            searchRadius = SEARCH_RADIUS_FAST;
        } else if (servoSpeed < SPEED_THRESHOLD_SLOW) {
            searchRadius = SEARCH_RADIUS_SLOW;
        } else {
            float t = (servoSpeed - SPEED_THRESHOLD_SLOW) / (SPEED_THRESHOLD_FAST - SPEED_THRESHOLD_SLOW);
            searchRadius = SEARCH_RADIUS_SLOW + t * (SEARCH_RADIUS_FAST - SEARCH_RADIUS_SLOW);
        }
        
        int searchCenterX = constrain(predictedX, 30, 210);
        int searchCenterY = constrain(predictedY, 30, 210);
        
        // ════════════════════════════════════════════════════════
        // STAGE 1: COARSE SEARCH (4-pixel grid)
        // ════════════════════════════════════════════════════════
        
        float bestCoarseConf = 0.0;
        int bestCoarseX = searchCenterX;
        int bestCoarseY = searchCenterY;
        float bestSkinPercentage = 0.0;
        
        int xStart = max(30, searchCenterX - searchRadius);
        int xEnd = min(210, searchCenterX + searchRadius);
        int yStart = max(30, searchCenterY - searchRadius);
        int yEnd = min(210, searchCenterY + searchRadius);
        
        for (int y = yStart; y < yEnd; y += COARSE_STEP) {
            for (int x = xStart; x < xEnd; x += COARSE_STEP) {
                CandidateResult result = evaluateCandidate(frame, x, y);
                if (result.valid && result.confidence > bestCoarseConf) {
                    bestCoarseConf = result.confidence;
                    bestCoarseX = x;
                    bestCoarseY = y;
                    bestSkinPercentage = result.skinPercentage;
                }
            }
        }
        
        // ════════════════════════════════════════════════════════
        // SKIN COLLAPSE CHECK (before fine search)
        // ════════════════════════════════════════════════════════
        
        if (bestSkinPercentage < SKIN_COLLAPSE_THRESHOLD) {
            consecutiveCollapses++;
            if (consecutiveCollapses >= 2) {
                signatureValid = false;  // Face definitely gone
            }
            histogramOnlyFrames++;
            return false;
        }
        consecutiveCollapses = 0;
        
        // Basic confidence check
        if (bestCoarseConf < CONFIDENCE_THRESHOLD * 0.9) {  // Slightly lower for coarse
            histogramOnlyFrames++;
            return false;
        }
        
        // ════════════════════════════════════════════════════════
        // STAGE 2: FINE SEARCH (1-pixel grid around coarse best)
        // ════════════════════════════════════════════════════════
        
        float bestFineConf = bestCoarseConf;
        int bestFineX = bestCoarseX;
        int bestFineY = bestCoarseY;
        
        int fineXStart = max(30, bestCoarseX - FINE_RADIUS);
        int fineXEnd = min(210, bestCoarseX + FINE_RADIUS);
        int fineYStart = max(30, bestCoarseY - FINE_RADIUS);
        int fineYEnd = min(210, bestCoarseY + FINE_RADIUS);
        
        for (int y = fineYStart; y <= fineYEnd; y += FINE_STEP) {
            for (int x = fineXStart; x <= fineXEnd; x += FINE_STEP) {
                // Skip the coarse point we already checked
                if (x == bestCoarseX && y == bestCoarseY) continue;
                
                CandidateResult result = evaluateCandidate(frame, x, y);
                if (result.valid && result.confidence > bestFineConf) {
                    bestFineConf = result.confidence;
                    bestFineX = x;
                    bestFineY = y;
                    bestSkinPercentage = result.skinPercentage;
                }
            }
        }
        
        // ════════════════════════════════════════════════════════
        // VALIDATION
        // ════════════════════════════════════════════════════════
        
        float finalConfidence = bestFineConf;
        
        // Coherence check on final position
        float coherence = calculateCoherence(frame, bestFineX, bestFineY, 15);
        if (coherence < MIN_COHERENCE_SCORE) {
            histogramOnlyFrames++;
            return false;
        }
        finalConfidence *= (0.92 + 0.08 * coherence);
        
        // Distance from predicted position
        float dx = bestFineX - predictedX;
        float dy = bestFineY - predictedY;
        float dist = sqrt(dx*dx + dy*dy);
        if (dist > MATCH_DISTANCE_LIMIT) {
            histogramOnlyFrames++;
            return false;
        }
        
        // Proximity bonus
        finalConfidence *= (1.0 - (dist / (float)searchRadius) * 0.03);
        
        // Final confidence check
        if (finalConfidence < CONFIDENCE_THRESHOLD) {
            histogramOnlyFrames++;
            return false;
        }
        
        // ════════════════════════════════════════════════════════
        // SUCCESS - Update state
        // ════════════════════════════════════════════════════════
        
        // Update quality history
        updateHistory(finalConfidence, bestFineX, bestFineY);
        
        // Update position tracking
        float jump = sqrt(pow(bestFineX - lastMatchX, 2) + pow(bestFineY - lastMatchY, 2));
        if (jump < MAX_POSITION_JUMP) {
            consecutiveStableFrames++;
        } else {
            consecutiveStableFrames = 0;
        }
        
        lastMatchX = bestFineX;
        lastMatchY = bestFineY;
        lastConfidence = finalConfidence;
        centerX = bestFineX;
        centerY = bestFineY;
        histogramOnlyFrames++;
        
        outX = bestFineX;
        outY = bestFineY;
        outConfidence = finalConfidence;
        
        return true;
    }
    
    bool isSignatureValid() const {
        if (!signatureValid) return false;
        return (millis() - signatureTime) <= SIGNATURE_MAX_AGE_MS;
    }
    
    unsigned long getSignatureAge() const {
        if (!signatureValid) return 9999;
        return millis() - signatureTime;
    }
    
    int getHistogramOnlyFrames() const {
        return histogramOnlyFrames;
    }
    
    TrackingQuality getTrackingQuality() {
        return assessQuality();
    }
    
    void invalidate() {
        signatureValid = false;
    }
};

#endif // HISTOGRAM_TRACKER_H