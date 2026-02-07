# PASS 1: PROTOCOL AGENT — Communication Protocol Review

**Agent Role:** Communication protocol specialist
**Date:** 2026-02-07
**Files Reviewed:** AIBridge.h, ReflexiveControl.h, Buddy_VersionflxV18.ino (parseVisionData), buddy_vision.py, buddy_web_full_V2.py, Buddy_esp32_cam_V18_debug.ino

---

## FINDING P1-1: CRITICAL — WebSocket Command Path to Teensy Is Non-Functional

**Severity:** CRITICAL — commands via WebSocket → ESP32 → Teensy UART are silently dropped

**Analysis:**

The `buddy_web_full_V2.py` server's WebSocket mode sends `!CMD\n` commands via:
```
Python → WebSocket (port 81) → ESP32 → UART (Serial1@921600) → Teensy
```

However, the Teensy firmware handles `!` commands ONLY on `Serial` (USB), NOT on `Serial1` (ESP32 UART):

- `serialEvent()` (Buddy_VersionflxV18.ino:863) reads from `Serial` (USB) and dispatches `!` to `aiBridge.handleCommand()`
- `parseVisionData()` (Buddy_VersionflxV18.ino:227) reads from `ESP32_SERIAL` (Serial1) but ONLY handles `FACE:`, `NO_FACE`, and `ESP32_READY` messages
- Any `!QUERY`, `!SPEAKING`, `!IDLE`, etc. arriving on Serial1 is read by `parseVisionData()`, fails all prefix checks, and is silently discarded

**Second problem:** Even if command parsing were added to Serial1, all AIBridge responses go to `Serial.print()` (USB), not `Serial1`:

```cpp
// AIBridge.h:244 — all responses go to Serial (USB)
Serial.print("{\"arousal\":");
Serial.print(emo.getArousal(), 2);
// ... entire response on Serial (USB)
Serial.println("}");
```

The ESP32 bridge on the other end of Serial1 would never receive the response.

**Impact:** The `"teensy_comm_mode": "websocket"` mode in buddy_web_full_V2.py cannot work. The code silently falls back to USB serial via `connect_teensy_serial()` in `connect_teensy_ws()` (line 606).

**Fix:** The Teensy firmware needs two changes:

1. Add `!` command handling on Serial1:
```cpp
// In Buddy_VersionflxV18.ino parseVisionData(), after existing message handling:
else if (buffer[0] == '!') {
    // AI command received via ESP32 bridge
    aiBridge.handleCommand(buffer + 1, &ESP32_SERIAL);  // Pass which serial to respond on
}
```

2. Make AIBridge responses go to the correct serial port:
```cpp
// In AIBridge.h — add a response serial parameter
Stream* responseStream = &Serial;  // Default to USB

void handleCommand(const char* cmdLine, Stream* respondTo = nullptr) {
    if (respondTo) responseStream = respondTo;
    // ... existing dispatch code ...
}

// Replace all Serial.print() with responseStream->print() in response methods
```

---

## FINDING P1-2: CRITICAL — No `/stream` Endpoint on ESP32

**Severity:** CRITICAL — buddy_vision.py cannot connect to camera stream

**Analysis:**

`buddy_vision.py` (line 47) configures:
```python
"stream_url": "http://{ip}/stream"
```

And the stream_receiver_thread (line 263) connects via:
```python
cap = cv2.VideoCapture(url)  # url = http://<ip>/stream
```

However, the ESP32 firmware (`Buddy_esp32_cam_V18_debug.ino`) only has these HTTP endpoints:
- `/health` (line 393) — lightweight health check
- `/capture` (line 408) — single JPEG frame capture
- Root `/` (line 477) — not found handler listing available endpoints

There is **no `/stream` MJPEG endpoint**. The old ESP32 firmware does single-frame captures, not continuous streaming.

**Impact:** The entire PC vision pipeline (buddy_vision.py) cannot receive frames from the ESP32. Face detection on the PC never runs. The system falls back to the ESP32's local face detection (old architecture).

**Fix:** The new ESP32 firmware (Package 1) must implement an MJPEG stream endpoint. Typical implementation:

```cpp
void handleStream() {
    WiFiClient client = server.client();
    String response = "HTTP/1.1 200 OK\r\nContent-Type: multipart/x-mixed-replace;boundary=frame\r\n\r\n";
    client.print(response);
    while (client.connected()) {
        camera_fb_t *fb = esp_camera_fb_get();
        if (fb) {
            client.printf("--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n", fb->len);
            client.write(fb->buf, fb->len);
            client.print("\r\n");
            esp_camera_fb_return(fb);
        }
        esp_task_wdt_reset();
    }
}
```

**Note:** The blocking `while(client.connected())` loop means the ESP32 HTTP server cannot handle other requests during streaming (see P4-1).

---

## FINDING P1-3: SIGNIFICANT — UDP Face Data Forwarding Not Implemented on ESP32

**Severity:** SIGNIFICANT — face data from PC cannot reach Teensy

**Analysis:**

`buddy_vision.py` sends face coordinates via UDP (line 427):
```python
udp_sock.sendto(msg.encode(), esp32_addr)  # (esp32_ip, 8888)
```

But the ESP32 firmware has no UDP listener. It doesn't:
1. Listen on UDP port 8888
2. Receive face data packets
3. Forward them to TeensySerial

The old ESP32 does ALL face detection locally and sends results directly to Teensy. It has no mechanism to receive external face data.

**Impact:** Even if the `/stream` endpoint existed, face detection results from the PC could not reach Teensy. The entire Package 2 data path is broken.

**Fix:** Package 1 (new ESP32 firmware) must implement UDP reception and forwarding:

```cpp
WiFiUDP udp;
const int UDP_PORT = 8888;

void setup() {
    // ... after WiFi connect ...
    udp.begin(UDP_PORT);
}

void loop() {
    // Forward UDP face data to Teensy
    int packetSize = udp.parsePacket();
    if (packetSize > 0) {
        char buf[128];
        int len = udp.read(buf, sizeof(buf) - 1);
        buf[len] = '\0';
        TeensySerial.println(buf);  // Forward as-is to Teensy
    }
}
```

---

## FINDING P1-4: MODERATE — Face Velocity (vx, vy) Parsed But Discarded by Teensy

**Severity:** MODERATE — wasted bandwidth, degraded tracking prediction

**Analysis:**

`buddy_vision.py` calculates and sends velocity in the FACE message:
```python
msg = f"FACE:{tx},{ty},{vx},{vy},{tw},{th},{conf},{seq}"
```

The Teensy's `parseVisionData()` (line 272-275) parses all 8 fields:
```cpp
int x, y, vx, vy, w, h, conf;
unsigned long sequence = 0;
int parsed = sscanf(latestFace + 5, "%d,%d,%d,%d,%d,%d,%d,%lu",
                    &x, &y, &vx, &vy, &w, &h, &conf, &sequence);
```

But `vx` and `vy` are LOCAL variables that are NEVER assigned to `currentFace` or passed to `reflexController`. The parsed velocity is discarded.

`ReflexiveControl.h` calculates its own velocity internally from position changes (lines 498-507).

**Impact:**
- The externally-calculated velocity (with better time resolution) is wasted
- `ReflexiveControl` recalculates velocity from position deltas, which can spike if face data arrives in bursts
- Not a correctness bug, but a missed optimization

**Fix:** Either:
1. Pass external velocity to ReflexiveControl (preferred):
```cpp
// In parseVisionData():
reflexController.updateFaceData(x, y, w, currentFace.distance);
reflexController.updateConfidence(conf);
reflexController.setExternalVelocity(vx, vy);  // New method
```
2. Or remove vx/vy from the message to save bandwidth (not recommended — better to use them).

---

## FINDING P1-5: MODERATE — QUERY Response Size Approaching Buffer Limits

**Severity:** MODERATE — will become critical if more fields are added

**Analysis:**

The QUERY JSON response from AIBridge.h `sendStateJSON()` (lines 244-302) currently outputs approximately 380-420 bytes, including:
- Emotion state (arousal, valence, dominance, emotion label)
- Needs (stimulation, social, energy, safety, novelty)
- Tracking state (tracking, animating, servo positions)
- Consciousness state (epistemic, tension, wondering, selfAwareness)
- Speech urge (speechUrge, speechTrigger, wantsToSpeak)

The new ESP32 bridge (Package 1) would need a `teensyRxBuffer` to hold the response before sending over WebSocket. If this buffer is 512 bytes (as suggested in the task), the current response fits but with only ~100 bytes of headroom.

Adding more consciousness fields, selfDescription strings, or moodTrend would exceed the buffer.

**Fix:** Either:
1. Use a larger buffer on ESP32 (1024 bytes)
2. Or implement chunked/streaming response forwarding on ESP32

---

## FINDING P1-6: LOW — Sequence Number Reset Not Handled

**Severity:** LOW — no functional impact with current code

**Analysis:**

`buddy_vision.py` uses a monotonic counter (line 156-159) for sequence numbers. If the vision pipeline restarts, the counter resets to 0.

The Teensy parses the sequence number (line 274) and stores it in `currentFace.sequence` (line 296) but **never validates or compares** it. It doesn't reject out-of-order or duplicate messages.

**Impact:** None currently. Sequence numbers are stored but unused by the Teensy.

---

## FINDING P1-7: LOW — Baud Rate Mismatch in Configuration

**Severity:** LOW — cosmetic, doesn't affect operation

**Analysis:**

`buddy_web_full_V2.py` CONFIG (line 98):
```python
"teensy_baud": 115200,
```

This is for USB serial fallback mode and correctly matches the Teensy's `Serial.begin(115200)` (line 352).

The ESP32-Teensy UART is at 921600 baud — this is handled separately and matches between:
- ESP32: `TeensySerial.begin(921600, ...)` (line 507)
- Teensy: `ESP32_SERIAL.begin(921600)` (line 362)

No mismatch exists.

---

## SUMMARY

| Finding | Severity | Status |
|---------|----------|--------|
| P1-1: WebSocket command path non-functional | CRITICAL | Requires Teensy firmware changes |
| P1-2: No /stream endpoint on ESP32 | CRITICAL | Requires new ESP32 firmware |
| P1-3: No UDP forwarding on ESP32 | SIGNIFICANT | Requires new ESP32 firmware |
| P1-4: Velocity parsed but discarded | MODERATE | Enhancement opportunity |
| P1-5: QUERY response approaching buffer limit | MODERATE | Monitor and plan |
| P1-6: Sequence number unused | LOW | No action needed |
| P1-7: Baud rate config correct | LOW | No action needed |

**Note:** P1-1, P1-2, and P1-3 together mean the new wireless architecture (Packages 1-3) is NOT functional with the current ESP32 firmware. The system currently operates in USB serial fallback mode with the old ESP32 firmware doing local face detection.
