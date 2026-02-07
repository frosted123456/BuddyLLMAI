# BUDDY FIXES AND VISION AUTONOMY — Implementation Document
## All code changes organized by file
## Date: 2026-02-07

---

## Summary

This document describes all changes implemented across three phases:

- **Phase 1**: Critical bug fixes from FINAL_REVIEW_REPORT.md
- **Phase 2**: Autonomous observation loop (VISION feedback command)
- **Phase 3**: Verification report (see PHASE3_VERIFICATION.md)

---

## Changes by File

### 1. `Buddy_VersionflxV18/AIBridge.h`

**Phase 1A (BUG-1): Response stream routing**
- Added `Stream* responseStream` member variable (default: `&Serial`)
- Added `handleCommand(const char* cmdLine, Stream* respondTo)` overload
- Replaced ALL `Serial.print`/`Serial.println` in response methods with `responseStream->print`/`responseStream->println`
- `updateStreaming()` explicitly saves/restores responseStream to always broadcast on USB Serial
- Total: ~80 replacements

**Phase 2: VISION command**
- Added `!VISION:json` to command list documentation
- Added `VISION:` case to `handleCommand()` dispatcher
- Added `cmdVision(const char* jsonStr)` method that:
  - Parses compact JSON: `{"f":1,"fc":2,"ex":"happy","nv":0.45,"ob":3,"mv":0.2}`
  - Feeds scene novelty into `spatialMemory.injectExternalNovelty()`
  - Feeds expressions into `emotion.nudge()` (emotional resonance)
  - Feeds face count into `needs.satisfySocial()` (social stimulation)
  - Feeds movement/objects into `needs.satisfyStimulation()`
  - Feeds high novelty into `consciousness.onEnvironmentChange()`
  - Sends NO response (fire-and-forget for UART bandwidth)

### 2. `Buddy_VersionflxV18/Buddy_VersionflxV18.ino`

**Phase 1A (BUG-1): Serial1 command routing**
- Added `!` prefix handling in `parseVisionData()` drain loop
- Commands starting with `!` route to `aiBridge.handleCommand(buffer + 1, &ESP32_SERIAL)`
- Responses go back to ESP32_SERIAL (not USB Serial)
- Increased `parseVisionData()` buffer from 128 to 256 bytes (Phase 2 safety)

**Phase 2: VISION fast path**
- Added `!VISION:` handler BEFORE general `!` handler
- Calls `aiBridge.cmdVision()` directly (no response routing needed)

### 3. `Buddy_VersionflxV18/SpatialMemory.h`

**Phase 2: External novelty injection**
- Added `injectExternalNovelty(int direction, float novelty)` method
- Blends PC-detected scene novelty (70%) with existing ultrasonic novelty (30%)
- Updates `lastUpdate` timestamp

### 4. `Buddy_VersionflxV18/Emotion.h`

**Phase 2: External emotional nudge**
- Added `nudge(float valenceShift, float arousalShift)` method
- Small constrained shifts for environmental influences
- Allows camera-observed expressions to influence Buddy's emotional state

### 5. `Buddy_VersionflxV18/ConsciousnessLayer.h`

**Phase 2: Environment change awareness**
- Added `WONDER_EXTERNAL` to `WonderingType` enum
- Added `float environmentalStimulation` member (initialized to 0.0)
- Added `onEnvironmentChange(float noveltyLevel)` method:
  - Triggers `WONDER_EXTERNAL` wondering if novelty > 0.7
  - 60-second cooldown between triggers
  - Updates `environmentalStimulation` level
- Added "What just changed?" to WONDER_EXTERNAL diagnostic output

### 6. `buddy_web_full_V2.py`

**Phase 1B (BUG-3): Ollama timeout**
- Replaced `query_ollama()` with threaded timeout version (default 60s)
- Raises `TimeoutError` if Ollama doesn't respond
- Emits error log via SocketIO

**Phase 1C (BUG-4): Processing lock**
- Replaced bare `is_processing = False` with `processing_lock = threading.Lock()`
- `process_input()` uses `processing_lock.acquire(blocking=False)` + `finally: release()`
- `process_spontaneous_speech()` uses same pattern
- All `is_processing` checks replaced with `processing_lock.locked()`
- Total: 8 references updated

**Phase 1H (ISSUE-4): WebSocket mode recovery**
- Removed `CONFIG["teensy_comm_mode"] = "serial"` in `connect_teensy_serial()`
- Allows WebSocket mode to retry after ESP32 reboot

**Phase 1H (ISSUE-5): API key from environment**
- Replaced hardcoded Picovoice API key with `os.environ.get("PICOVOICE_ACCESS_KEY", "")`

**Phase 1H (OPT-3): Ollama model validation**
- Added model availability check at startup before `connect_teensy()`
- Warns if configured model is not found in Ollama

### 7. `buddy_vision.py`

**Phase 1D (BUG-5): Frame buffer minimization**
- Added `cap.set(cv2.CAP_PROP_BUFFERSIZE, 1)` after `cv2.VideoCapture()` creation
- Reduces internal OpenCV buffering from 5+ frames to 1

**Phase 2: VISION update sender**
- Added UDP socket creation for VISION updates in `rich_vision_thread()`
- At end of each rich vision cycle, sends `!VISION:{json}` via UDP to ESP32
- Uses `json.dumps()` with compact separators
- Fire-and-forget (exception caught silently)

### 8. `Buddy_ESP32_Bridge/Buddy_ESP32_Bridge.ino` (NEW FILE)

**Phase 1E/1F/1G: Complete ESP32 WiFi Bridge firmware**

This is the missing Package 1 firmware that enables the full wireless architecture:

- **MJPEG stream** on `/stream` for PC vision pipeline
- **Single frame capture** on `/capture`
- **Health endpoint** on `/health`
- **WebSocket server** on port 81 for AI commands (PC ↔ Teensy)
- **UDP listener** on port 8888 for face data (PC → Teensy)

**Phase 1E: UART mutex**
- `SemaphoreHandle_t uartMutex` prevents interleaving
- UDP face data: 5ms mutex wait, drops frame if busy
- WebSocket commands: 300ms mutex wait, returns timeout error if busy

**Phase 1F: Dual-core architecture**
- Core 0: `captureTask()` (camera frames) + `httpServerTask()` (HTTP/stream)
- Core 1: `loop()` (WebSocket + UDP + Teensy unsolicited messages)
- Stream never blocks command processing

**Phase 1G: Large response buffer**
- `teensyRxBuffer[1024]` (was 512 in original design)
- Overflow protection: resets buffer position if limit reached

---

## Data Flow After Implementation

```
Camera (on Buddy's head)
    |
    +---> MJPEG stream over WiFi ---> Server PC
    |                                     |
    |                           +---------+---------+
    |                           |                   |
    |                     Face Detection       Rich Vision
    |                      (30 fps)           (2-3 fps)
    |                           |                   |
    |                     FACE:x,y,...        !VISION:{...}
    |                     (UDP fast)          (UDP slow)
    |                           |                   |
    |                           +--------+----------+
    |                                    |
    |                              ESP32 Bridge
    |                             (UART mutex)
    |                                    |
    |                           UART --> Teensy
    |                                    |
    |                       +------------+------------+
    |                       |                         |
    |                 parseVisionData()         !VISION handler
    |                       |                         |
    |                 ReflexiveControl          cmdVision()
    |                 (face tracking)        +- emotion.nudge()
    |                       |               +- spatialMemory.inject()
    |                       |               +- needs.satisfy()
    |                       |               +- consciousness.onChange()
    |                       |                         |
    |                       +------------+------------+
    |                                    |
    |                       +------------+------------+
    |                       |                         |
    |                 Servo Movement          Behavior Selection
    |                 (track face)       (explore? engage? retreat?)
    |                       |                         |
    |                       +------------+------------+
    |                                    |
    +---- Camera moves (eye-on-hand) ----+
```

## Emergent Behavioral Cycles

1. **Social resonance**: See person smile -> valence rises -> become playful -> person smiles more
2. **Exploration drive**: Scene changes -> novelty rises -> explore behavior -> camera pans -> new scene
3. **Loneliness loop**: Nobody around -> social need builds -> lonely -> spontaneous speech
4. **Caution response**: See frown -> valence drops -> cautious behavior -> approach more carefully
5. **Group engagement**: Multiple people -> extra social stimulation -> more animated behavior
6. **Discovery wonder**: Novel object -> consciousness wondering -> "what's that?" speech
