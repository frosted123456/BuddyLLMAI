# PASS 4: INTEGRATION AGENT — System Integration Review

**Agent Role:** System integration specialist
**Date:** 2026-02-07
**Files Reviewed:** All package files, ESP32 firmware, Teensy firmware, Python scripts

---

## FINDING I4-1: CRITICAL — Package 1 (New ESP32 Firmware) Not Present in Repository

**Severity:** CRITICAL — the new wireless architecture cannot operate

**Analysis:**

The entire system migration (Packages 1-3) depends on a new ESP32 firmware that acts as:
1. MJPEG camera stream server (`/stream` endpoint)
2. WebSocket server (port 81) for Python → Teensy commands
3. UDP listener (port 8888) for face data from buddy_vision.py
4. WiFi-UART bridge forwarding between Python and Teensy

**The repository contains only the OLD ESP32 firmware** (`buddyesp32cam-main/...`) which:
- Does local face detection (EloquentEsp32cam library)
- Has HTTP `/capture` and `/health` endpoints only
- Has no WebSocket server
- Has no UDP listener
- Has no MJPEG streaming
- Sends face data directly to Teensy via UART (old architecture)

**The Python files (buddy_vision.py, buddy_web_full_V2.py) ARE implemented** for the new architecture, but their ESP32 counterpart doesn't exist.

**Impact:** The system runs in degraded mode:
- buddy_vision.py cannot connect to `/stream` → face detection on PC is non-functional
- buddy_web_full_V2.py falls back from WebSocket to USB serial for Teensy commands
- Face tracking uses old ESP32 local detection (functional but lower quality than MediaPipe)

**Fix:** Implement the Package 1 ESP32 firmware with all required components. This is the highest-priority development task.

---

## FINDING I4-2: SIGNIFICANT — Missing QUERY Response Fields for Spontaneous Speech

**Severity:** SIGNIFICANT — spontaneous speech prompts are degraded

**Analysis:**

`buddy_web_full_V2.py` `build_spontaneous_prompt()` (line 1310-1313) reads:
```python
self_desc = state.get('selfDescription', '')
wondering = state.get('wondering', '')
mood_trend = state.get('moodTrend', '')
```

But `AIBridge.h` `sendStateJSON()` (lines 244-302) does NOT output:
- `selfDescription` — never included in QUERY response
- `moodTrend` — never included in QUERY response

The `wondering` field is output but as a boolean (`true`/`false`), not as the wondering text content. `build_spontaneous_prompt()` uses it as a string (line 1334):
```python
f"'{wondering or 'something about your existence'}'"
```

Since `wondering` comes back as `true` (boolean/string), the prompt becomes:
```
"wondering: 'True'"
```
Instead of the actual wondering text.

**Impact:**
- Spontaneous speech prompts lack self-description context
- Wondering prompts use literal "True" instead of actual wondering content
- Mood trend information is never available

**Fix — Option A:** Add missing fields to AIBridge sendStateJSON():
```cpp
// In sendStateJSON(), after existing consciousness fields:
Serial.print(",\"selfDescription\":\"");
Serial.print(consciousness.getSelfDescription());  // If method exists
Serial.print("\"");

Serial.print(",\"moodTrend\":\"");
// Calculate from narrative
Serial.print(consciousness.getMoodTrend() > 0 ? "improving" : "declining");
Serial.print("\"");
```

**Fix — Option B:** Fix Python to use available data:
```python
# Use the boolean 'wondering' correctly
is_wondering = state.get('wondering', False)
wondering_text = ''
if is_wondering:
    wondering_text = 'something about your existence'  # Generic fallback
```

---

## FINDING I4-3: SIGNIFICANT — Picovoice Access Key Hardcoded in Source

**Severity:** SIGNIFICANT — security issue if repository is public

**Analysis:**

`buddy_web_full_V2.py` (line 71):
```python
"picovoice_access_key": "wUO0BjvmEl2gQDwJaRh18jodPKKkGWGU+YBBC1+F+6CVdIvG0HFwPQ=="
```

This is a Picovoice API key committed directly in source code.

**Impact:** If this repository is made public (or already is), the API key is exposed and could be:
- Used by unauthorized parties, consuming the owner's quota
- Revoked by Picovoice for public exposure

**Fix:**
```python
import os

CONFIG = {
    "picovoice_access_key": os.environ.get("PICOVOICE_ACCESS_KEY", ""),
    # ...
}
```

And add to `.gitignore`:
```
.env
```

---

## FINDING I4-4: MODERATE — Old ESP32 Camera Resolution Hardcoded in Multiple Places

**Severity:** MODERATE — creates coupling between ESP32 camera config and Teensy assumptions

**Analysis:**

The 240x240 resolution appears as hardcoded constants in multiple locations:

**Teensy (ReflexiveControl.h:33-36):**
```cpp
#define CAMERA_CENTER_X 120
#define CAMERA_CENTER_Y 120
#define CAMERA_FRAME_WIDTH 240
#define CAMERA_FRAME_HEIGHT 240
```

**Teensy (Buddy_VersionflxV18.ino:153-154, 279):**
```cpp
int centerX = 120;
if (x < 0 || x > 240 || y < 0 || y > 240 || ...)
```

**ESP32 (Buddy_esp32_cam_V18_debug.ino:167-170):**
```cpp
const int CAMERA_WIDTH = 240;
const int CAMERA_HEIGHT = 240;
const int CENTER_X = 120;
const int CENTER_Y = 120;
```

**buddy_vision.py (lines 50-53):**
```python
"teensy_frame_width": 240,
"teensy_frame_height": 240,
"teensy_center_x": 120,
"teensy_center_y": 120,
```

If the new ESP32 firmware changes to VGA (640×480), buddy_vision.py's `map_to_teensy_coords()` correctly maps to 240×240. But any assumptions about aspect ratio (square vs 4:3) would break. VGA is 4:3, not 1:1.

**Impact:** Coordinates mapped from a 4:3 frame to a 1:1 grid will have distorted face positions. A face at the left edge of a 640×480 frame maps differently than a face at the left edge of a 240×240 frame due to aspect ratio mismatch.

**Fix:** Either:
1. Keep square resolution on new ESP32 (e.g., 320×320, mapped to 240×240)
2. Or adjust coordinate mapping to handle aspect ratio:
```python
# Preserve aspect ratio by mapping using the smaller dimension
scale = min(tw_target / frame_w, th_target / frame_h)
tx = int(x * scale + (tw_target - frame_w * scale) / 2)
ty = int(y * scale + (th_target - frame_h * scale) / 2)
```

---

## FINDING I4-5: MODERATE — No Rotation Validation or Default

**Severity:** MODERATE — incorrect rotation silently breaks tracking

**Analysis:**

The old ESP32 firmware rotates frames 90° CCW in software:
```cpp
// Buddy_esp32_cam_V18_debug.ino:302-307
if (enableRotation && rotationBuffer != NULL) {
    rotateFrame90CCW(camera.frame->buf, rotationBuffer, 240, 240);
    memcpy(camera.frame->buf, rotationBuffer, 240 * 240 * 2);
}
```

In the new architecture, `buddy_vision.py` handles rotation via `--rotate` flag (line 281-287). But:
- `start_buddy.py` passes `--rotate` from command line (defaults to 0)
- If the user forgets `--rotate 90` (common camera mounting), face coordinates are rotated 90° from expected
- Tracking would move vertically when it should move horizontally
- There's no auto-detection or warning about incorrect rotation

**Impact:** User-experience issue — if camera is mounted sideways and `--rotate` is wrong, tracking appears completely broken.

**Fix:** Add rotation verification at startup:
```python
# In face_tracking_thread, after first detection:
if first_detection:
    face_center_x = best_detection["x"]
    if frame_w > frame_h * 1.2:  # Landscape frame from portrait camera?
        print("[WARNING] Frame appears to be landscape but camera may be portrait-mounted.")
        print("          If tracking seems wrong, try: --rotate 90")
```

---

## FINDING I4-6: MODERATE — Edge TTS Requires Internet (No Fallback)

**Severity:** MODERATE — TTS silently fails without internet

**Analysis:**

`buddy_web_full_V2.py` uses `edge_tts` for text-to-speech (line 1058-1063):
```python
async def generate_tts(text):
    await edge_tts.Communicate(text, CONFIG["tts_voice"], rate=CONFIG["tts_rate"]).save(tp)
```

Edge TTS requires an internet connection to Microsoft's servers. If the server PC has no internet:
- `generate_tts()` raises an exception
- The exception is caught in `process_input()` (line 1142-1148)
- Error is emitted via SocketIO
- But the user doesn't hear a response — Buddy appears to think but never speaks

**Fix:** Add fallback TTS or clear error message:
```python
async def generate_tts(text):
    try:
        # ... existing edge_tts code ...
    except Exception as e:
        socketio.emit('log', {
            'message': f'TTS failed (internet required): {e}',
            'level': 'error'
        })
        # Fallback: return the text for display instead
        socketio.emit('response', {'text': f'[TTS offline] {text}'})
        return None
```

---

## FINDING I4-7: MODERATE — Ollama Model Validation Missing

**Severity:** MODERATE — cryptic error if wrong model is configured

**Analysis:**

`buddy_web_full_V2.py` CONFIG (line 89):
```python
"ollama_model": "llava",
```

`query_ollama()` (line 1056) calls:
```python
ollama.chat(model=CONFIG["ollama_model"], messages=msgs)
```

If the user doesn't have "llava" installed in Ollama, the call fails with an unhelpful error. There's no startup validation.

**Fix:** Add model check at startup:
```python
def validate_ollama():
    try:
        models = ollama.list()
        available = [m['name'] for m in models['models']]
        if CONFIG['ollama_model'] not in available:
            # Try partial match
            matches = [m for m in available if CONFIG['ollama_model'] in m]
            if matches:
                CONFIG['ollama_model'] = matches[0]
                print(f"  Using model: {matches[0]}")
            else:
                print(f"  WARNING: Model '{CONFIG['ollama_model']}' not found!")
                print(f"  Available: {', '.join(available)}")
    except Exception as e:
        print(f"  WARNING: Ollama not available: {e}")
```

---

## FINDING I4-8: LOW — Flask Secret Key Is Hardcoded

**Severity:** LOW — acceptable for local network use

**Analysis:**

`buddy_web_full_V2.py` (line 152):
```python
app.config['SECRET_KEY'] = 'buddy-secret-key'
```

A hardcoded secret key means session tokens are predictable. On a home network this is acceptable, but if the server is ever exposed to the internet, sessions could be forged.

**Fix:** Use a random key:
```python
app.config['SECRET_KEY'] = os.urandom(24).hex()
```

---

## SUMMARY

| Finding | Severity | Effort to Fix |
|---------|----------|---------------|
| I4-1: Package 1 ESP32 firmware missing | CRITICAL | High (full implementation) |
| I4-2: Missing QUERY fields for speech | SIGNIFICANT | Low (add Serial.print lines) |
| I4-3: API key in source code | SIGNIFICANT | Low (move to env var) |
| I4-4: Resolution hardcoded in many places | MODERATE | Medium (centralize constants) |
| I4-5: No rotation validation | MODERATE | Low (add warning) |
| I4-6: Edge TTS needs internet | MODERATE | Low (add fallback message) |
| I4-7: Ollama model not validated | MODERATE | Low (add startup check) |
| I4-8: Flask secret key hardcoded | LOW | Trivial |
