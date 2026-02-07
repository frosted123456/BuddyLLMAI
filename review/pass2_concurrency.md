# PASS 2: CONCURRENCY AGENT — Thread Safety Review

**Agent Role:** Thread safety and race condition specialist
**Date:** 2026-02-07
**Files Reviewed:** buddy_web_full_V2.py, buddy_vision.py, Buddy_esp32_cam_V18_debug.ino, ReflexiveControl.h

---

## FINDING C2-1: CRITICAL — `is_processing` Flag Has Race Condition

**Severity:** CRITICAL — can cause overlapping LLM queries, garbled audio, duplicate Teensy commands

**Analysis:**

`is_processing` is a bare `bool` global (buddy_web_full_V2.py:160):
```python
is_processing = False
```

It is checked and set without any synchronization in two different functions:

**process_input() (line 1067-1068):**
```python
def process_input(text, include_vision):
    global is_processing
    if is_processing: return  # CHECK
    is_processing = True       # SET (no atomicity between check and set)
```

**process_spontaneous_speech() (line 1236-1238):**
```python
def process_spontaneous_speech(prompt_text, trigger):
    global is_processing
    if is_processing: return  # CHECK
    is_processing = True       # SET
```

**Race scenario:**
1. Spontaneous speech thread checks `is_processing == False` ✓
2. Context switch — wake word thread checks `is_processing == False` ✓
3. Both threads set `is_processing = True`
4. Both threads proceed to call `query_ollama()` simultaneously
5. Both send overlapping Teensy commands (THINKING, SPEAKING, etc.)
6. Both generate TTS audio, both emit via SocketIO
7. Browser receives interleaved audio and responses

**Impact:** Garbled behavior — Buddy could start thinking animation, then immediately override with speaking animation. Two audio streams play simultaneously. Teensy receives contradictory commands.

**Fix:**
```python
# Replace bare bool with a threading.Lock:
processing_lock = threading.Lock()

def process_input(text, include_vision):
    if not processing_lock.acquire(blocking=False):
        return  # Already processing
    try:
        # ... existing code ...
    finally:
        processing_lock.release()

def process_spontaneous_speech(prompt_text, trigger):
    if not processing_lock.acquire(blocking=False):
        return
    try:
        # ... existing code ...
    finally:
        processing_lock.release()
```

---

## FINDING C2-2: SIGNIFICANT — `spontaneous_speech_lock` Provides No Actual Protection

**Severity:** SIGNIFICANT — wake word recording can overlap with spontaneous speech

**Analysis:**

In `record_and_process()` (buddy_web_full_V2.py:862-863):
```python
def record_and_process():
    # If spontaneous speech is happening, wait for it to finish
    with spontaneous_speech_lock:
        pass  # ← Acquires and immediately releases
```

This is intended to wait for spontaneous speech to finish, but `spontaneous_speech_lock` is only held briefly during `check_spontaneous_speech()` (lines 1197-1227), NOT during the actual speech generation in `process_spontaneous_speech()`.

**Timeline:**
1. `check_spontaneous_speech()` acquires lock (line 1197)
2. Starts thread running `process_spontaneous_speech()` (line 1220)
3. Releases lock in finally block (line 1227)
4. Thread runs independently — lock is FREE
5. `record_and_process()` acquires lock instantly (no waiting)
6. Recording proceeds while spontaneous speech is still in progress

**Impact:** Wake word recording can start while spontaneous speech is still generating. Both paths check `is_processing` but that has its own race condition (C2-1). Even with C2-1 fixed, the timing window between spontaneous speech starting and `is_processing` being set allows overlap.

**Fix:** Hold the lock for the duration of speech generation:
```python
def process_spontaneous_speech(prompt_text, trigger):
    with spontaneous_speech_lock:
        # ... entire speech pipeline ...
```

And in `record_and_process()`:
```python
def record_and_process():
    with spontaneous_speech_lock:  # Now this actually waits
        pass
    # ... recording continues ...
```

---

## FINDING C2-3: SIGNIFICANT — ESP32 UART Contention Between Face Data and Commands

**Severity:** SIGNIFICANT — affects the new architecture (Package 1) when implemented

**Analysis:**

In the new ESP32 bridge architecture:
- UDP face data arrives and is forwarded to TeensySerial
- WebSocket commands arrive and are forwarded to TeensySerial with response expected

Both share the same UART. The ESP32 runs single-threaded (Arduino `loop()`), but interleaving can occur between loop iterations:

1. WebSocket command arrives → ESP32 writes `!QUERY\n` to TeensySerial
2. ESP32 starts waiting for response on TeensySerial
3. UDP face data arrives during next loop iteration
4. ESP32 writes `FACE:120,120,...\n` to TeensySerial
5. Teensy processes FACE data, outputs debug to Serial1
6. ESP32 reads debug output thinking it's the QUERY response
7. WebSocket client gets garbage response

**Impact:** Command responses could be corrupted by interleaved face data in the new architecture.

**Fix:** The ESP32 bridge should implement a UART mutex or state machine:
```cpp
enum UartState { IDLE, WAITING_RESPONSE };
UartState uartState = IDLE;

// Only forward UDP when not waiting for command response
void handleUDP() {
    if (uartState == WAITING_RESPONSE) return;  // Drop face data during command
    // ... forward UDP ...
}

// Command path:
void handleWebSocketCommand(const char* cmd) {
    uartState = WAITING_RESPONSE;
    TeensySerial.println(cmd);
    // Read response with timeout
    // ...
    uartState = IDLE;
}
```

---

## FINDING C2-4: MODERATE — `teensy_connected` Flag Unprotected

**Severity:** MODERATE — can cause brief inconsistencies but self-corrects

**Analysis:**

`teensy_connected` is a global bool (buddy_web_full_V2.py:165) accessed from multiple threads without synchronization:

- Set in `connect_teensy_ws()` (line 595)
- Set in `connect_teensy_serial()` (line 616, 622)
- Read/written in `teensy_send_ws()` (line 641, 665)
- Read/written in `teensy_send_serial()` (line 672, 691)
- Read in `teensy_poll_loop()` (line 718)
- Read in `process_input()` via `get_buddy_state_prompt()` (line 984)

**Impact:** In Python, the GIL makes individual bool reads/writes atomic, so this won't cause corruption. But it can cause brief inconsistencies where one thread sees the old value. For example, `teensy_poll_loop()` could set `teensy_connected = False` while another thread is mid-way through `teensy_send_ws()`.

**Fix:** While technically safe under CPython's GIL, using `teensy_state_lock` consistently would be more robust:
```python
def set_teensy_connected(value):
    global teensy_connected
    with teensy_state_lock:
        teensy_connected = value
```

---

## FINDING C2-5: MODERATE — VisionState Has Unprotected Attribute Updates

**Severity:** MODERATE — minor data races on counters and flags

**Analysis:**

In `buddy_vision.py`, the `VisionState` class uses `self.lock` for critical operations, but several attributes are modified without the lock:

```python
# stream_receiver_thread (line 267-268):
state.stream_connected = False  # No lock

# stream_receiver_thread (line 303-304):
state.errors += 1        # No lock — read-modify-write race
state.last_error = str(e) # No lock

# face_tracking_thread (line 428):
state.udp_sent += 1      # No lock — read-modify-write race

# face_tracking_thread (line 493):
state.errors += 1        # No lock
```

**Impact:** Under CPython's GIL, these are mostly safe for simple assignments. The `+= 1` operations are technically not atomic (read-modify-write), but the worst case is a missed increment on an error counter.

**Fix:** Either:
1. Wrap all mutations in `with state.lock:` (thorough)
2. Use `threading.atomic` or ignore (pragmatic — counters are diagnostic only)

---

## FINDING C2-6: LOW — Debug Serial Output in ReflexiveControl May Cause Timing Issues

**Severity:** LOW — affects Teensy only, controlled by throttling

**Analysis:**

`ReflexiveControl.h` has debug Serial.print() calls in the hot path (lines 708-720, 787-799):
```cpp
static unsigned long lastDebug = 0;
if (millis() - lastDebug > 500) {
    Serial.print("[REFLEX] Face:(");
    // ... multiple print calls ...
    lastDebug = millis();
}
```

Serial.print() on Teensy is buffered but can block if the USB TX buffer is full (when no host is reading). At 115200 baud, each print call takes microseconds but can accumulate.

**Impact:** The 500ms throttle prevents this from being a real issue. During each debug burst (~100 bytes), UART time is ~870µs at 115200 baud. This is well within the 20ms loop budget.

**Fix:** No action needed. The throttling is appropriate. Consider wrapping in `#ifdef DEBUG_REFLEX` for release builds.

---

## SUMMARY

| Finding | Severity | Impact |
|---------|----------|--------|
| C2-1: `is_processing` race condition | CRITICAL | Overlapping LLM queries, garbled output |
| C2-2: spontaneous_speech_lock ineffective | SIGNIFICANT | Wake word overlaps spontaneous speech |
| C2-3: ESP32 UART contention (new arch) | SIGNIFICANT | Corrupted command responses |
| C2-4: teensy_connected unprotected | MODERATE | Brief inconsistencies |
| C2-5: VisionState unprotected counters | MODERATE | Missed counter increments |
| C2-6: Debug serial in hot path | LOW | Minimal impact with throttling |
