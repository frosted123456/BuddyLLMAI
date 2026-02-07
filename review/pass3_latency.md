# PASS 3: LATENCY AGENT — Real-Time Performance Review

**Agent Role:** Real-time performance specialist
**Date:** 2026-02-07
**Files Reviewed:** buddy_vision.py, ReflexiveControl.h, BehaviorEngine.h, Buddy_VersionflxV18.ino, buddy_web_full_V2.py

---

## End-to-End Face Tracking Latency Budget

| Stage | Component | Expected Time | Notes |
|-------|-----------|--------------|-------|
| 1 | ESP32 camera capture | ~10ms | Single frame, OV2640 |
| 2 | MJPEG encode (ESP32) | ~10ms | Hardware-assisted |
| 3 | WiFi TX to PC | ~2-5ms | Local network |
| 4 | OpenCV decode (PC) | ~3-5ms | MJPEG to BGR |
| 5 | **OpenCV buffer drain** | **0-165ms** | **SEE FINDING L3-1** |
| 6 | MediaPipe detection (PC) | ~15-30ms | Short-range model |
| 7 | UDP send to ESP32 | ~1ms | Non-blocking |
| 8 | ESP32 UART forward | ~2-3ms | At 921600 baud |
| 9 | Teensy parse + servo | ~1ms | sscanf + directWrite |
| **TOTAL (best)** | | **~45-60ms** | No buffer issue |
| **TOTAL (worst)** | | **~210ms** | With stale buffer |

---

## FINDING L3-1: CRITICAL — OpenCV VideoCapture Frame Buffer Causes Stale Data

**Severity:** CRITICAL — adds 50-165ms of latency to face tracking

**Analysis:**

`buddy_vision.py` stream_receiver_thread (line 263) opens the MJPEG stream:
```python
cap = cv2.VideoCapture(url)
```

OpenCV's VideoCapture internally buffers frames. The default buffer size is typically 5 frames. When the detection thread reads a frame from VisionState, it gets the latest frame stored by the stream thread. But the stream thread's `cap.read()` returns frames in FIFO order from OpenCV's internal buffer.

If the ESP32 streams at 15 FPS (67ms per frame) and the stream thread reads at 15 FPS:
- Buffer fills with 5 frames = 5 × 67ms = 335ms of data
- Each `cap.read()` returns a frame that's 1-5 frames old
- The frame stored in VisionState may be 67-335ms stale when detection runs

The stream receiver thread does read continuously (line 274-276), storing the latest frame:
```python
while cap.isOpened():
    ret, frame = cap.read()  # Returns OLDEST buffered frame
    state.update_frame(frame)
```

But each call to `cap.read()` returns the next buffered frame, not the latest. So if the stream sends frames faster than `cap.read()` can consume them, there's ALWAYS a buffer delay.

**Impact:** Face tracking reacts to where the person WAS 50-165ms ago, not where they ARE now. This causes visible lag and potential tracking overshoot.

**Fix — Option A (preferred):** Set buffer size to 1:
```python
cap = cv2.VideoCapture(url)
cap.set(cv2.CAP_PROP_BUFFERSIZE, 1)
```

**Fix — Option B:** Drain buffer before processing:
```python
while cap.isOpened():
    ret, frame = cap.read()
    if not ret:
        break
    # Drain: read all available without blocking
    while True:
        ret2, frame2 = cap.read()
        if not ret2:
            break
        frame = frame2  # Keep latest
    state.update_frame(frame)
```

**Fix — Option C:** Use a grab/retrieve pattern:
```python
# Grab discards the frame quickly, retrieve decodes
while True:
    cap.grab()  # Fast — just advances buffer
    if not cap.grab():  # Try to advance again
        break
cap.retrieve()  # Decode only the latest
```

---

## FINDING L3-2: SIGNIFICANT — ESP32 Stream Handler Blocks Entire HTTP Server

**Severity:** SIGNIFICANT — during streaming, all other HTTP endpoints are unreachable

**Analysis:**

The old ESP32 firmware uses Arduino's `WebServer.h` which handles one client at a time. If a `/stream` endpoint were added (as Package 1 requires), it would block in a `while(client.connected())` loop.

During streaming:
- `server.handleClient()` (line 636) never returns until stream client disconnects
- UDP face data cannot be received (loop() never advances)
- `/capture` endpoint is unreachable (for LLM vision snapshots)
- `/health` endpoint is unreachable

**Impact for Package 1:**
- If buddy_vision.py connects to `/stream`, nobody else can view the stream
- The LLM snapshot path (`capture_frame()` → ESP32 `/capture`) breaks
- UDP face data forwarding stops during streaming

**Fix:** Use the ESP32's `AsyncWebServer` library instead:
```cpp
#include <ESPAsyncWebServer.h>
AsyncWebServer server(80);

// Stream can run independently without blocking
server.on("/stream", HTTP_GET, [](AsyncWebServerRequest *request) {
    // AsyncResponseStream handles non-blocking streaming
});
```

Alternatively, use a dedicated camera task on Core 0 while HTTP runs on Core 1.

---

## FINDING L3-3: MODERATE — Reflex Controller Only Moves on Fresh Data (Correct but Has Side Effect)

**Severity:** MODERATE — correct design but causes visible jerkiness

**Analysis:**

The main loop (Buddy_VersionflxV18.ino:588) only moves servos when fresh face data arrives:
```cpp
if (reflexController.isActive() && currentFace.detected && freshFaceDataReceived) {
    freshFaceDataReceived = false;
    // ... calculate and move ...
}
```

This is CORRECT — it prevents processing stale data repeatedly. However:

- Face data arrives at ~10-15 Hz (from old ESP32) or ~15-30 Hz (from vision pipeline)
- The reflex loop runs at 50 Hz
- Between face updates, servos hold their last position with no interpolation
- With the 240x240 frame and PID control, each update can produce a noticeable jump

**Impact:** At 10 Hz face data, servos update every 100ms with discrete jumps instead of smooth continuous motion. This looks jerky compared to a system that interpolates between face positions.

**Fix — Enhancement:** Add interpolation between face data points:
```cpp
// Instead of only moving on fresh data, interpolate toward target
if (reflexController.isActive() && currentFace.detected) {
    if (freshFaceDataReceived) {
        freshFaceDataReceived = false;
        reflexController.calculate(currentBase, currentNod, targetBase, targetNod);
    } else {
        // Interpolate toward last target at reduced speed
        targetBase = currentBase + (targetBase - currentBase) * 0.3;
        targetNod = currentNod + (targetNod - currentNod) * 0.3;
    }
    servoController.directWrite(targetBase, targetNod, false);
}
```

---

## FINDING L3-4: MODERATE — Process Input LLM Query Has No Timeout

**Severity:** MODERATE — affects responsiveness, not tracking latency

**Analysis:**

`buddy_web_full_V2.py` `query_ollama()` (line 1056):
```python
def query_ollama(text, img=None):
    # ... prompt setup ...
    return ollama.chat(model=CONFIG["ollama_model"], messages=msgs)["message"]["content"]
```

The `ollama.chat()` call has NO timeout. If Ollama is slow (GPU contention, large model, swapping), this blocks indefinitely. During this time:

- `is_processing = True` blocks all other interaction
- The THINKING animation plays indefinitely on Teensy
- No spontaneous speech can trigger
- Wake word detection continues but recorded input is queued

**Impact:** User must wait for Ollama to respond. With LLaVA vision model processing an image on CPU, response times can exceed 60 seconds.

**Fix:**
```python
import signal

def query_ollama_with_timeout(text, img=None, timeout=45):
    """Query Ollama with a timeout."""
    result = [None]
    error = [None]

    def query():
        try:
            result[0] = query_ollama(text, img)
        except Exception as e:
            error[0] = e

    t = threading.Thread(target=query)
    t.start()
    t.join(timeout=timeout)

    if t.is_alive():
        raise TimeoutError(f"Ollama did not respond within {timeout}s")
    if error[0]:
        raise error[0]
    return result[0]
```

---

## FINDING L3-5: LOW — WebSocket Command Latency Acceptable for Spontaneous Speech

**Severity:** LOW — acceptable performance

**Analysis:**

`process_spontaneous_speech()` sends approximately 8 Teensy commands:
1. THINKING (or EXPRESS:curious)
2. STOP_THINKING
3. execute_buddy_actions (0-3 commands)
4. SPOKE
5. SPEAKING
6. STOP_SPEAKING (in finish thread)
7. IDLE (in finish thread)

Each `teensy_send_ws()` has a 500ms timeout. Average response time is ~50ms per command.

**Total command overhead:**
- Average: 8 × 50ms = 400ms
- Worst case: 8 × 500ms = 4,000ms (only if ESP32 is extremely slow)

Compared to the LLM query time (10-60s), command overhead is negligible.

---

## FINDING L3-6: LOW — Velocity Spike Potential in buddy_vision.py

**Severity:** LOW — velocity is ultimately unused by Teensy

**Analysis:**

`VisionState.get_velocity()` (buddy_vision.py:143-144):
```python
dt = now - self.prev_face_time
if dt > 0 and dt < 0.2 and self.prev_face_time > 0:
    vx = int((x - self.prev_face_x) / dt)
    vy = int((y - self.prev_face_y) / dt)
```

If `dt` is very small (e.g., 0.001s), a 1-pixel change produces velocity of 1000 px/s. No clamping is applied to the velocity before sending via UDP.

**Impact:** Velocity is parsed but discarded by Teensy (Finding P1-4), so this has no functional impact. If velocity use is ever added to Teensy, this would cause tracking instability.

**Fix (for future):**
```python
vx = max(-500, min(500, int((x - self.prev_face_x) / dt)))
vy = max(-500, min(500, int((y - self.prev_face_y) / dt)))
```

---

## SUMMARY

| Finding | Severity | Latency Impact |
|---------|----------|---------------|
| L3-1: OpenCV buffer stale frames | CRITICAL | +50-165ms tracking lag |
| L3-2: ESP32 stream blocks server | SIGNIFICANT | Blocks all endpoints during stream |
| L3-3: No servo interpolation | MODERATE | Visible jerkiness at 10Hz |
| L3-4: Ollama query no timeout | MODERATE | Can block forever |
| L3-5: WS command overhead OK | LOW | ~400ms total, acceptable |
| L3-6: Velocity spike potential | LOW | No impact (data unused) |

**Best achievable tracking latency:** ~45-60ms (with L3-1 fixed)
**Current effective latency:** ~100-225ms (with buffer issue)
