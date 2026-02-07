# PASS 5: ROBUSTNESS AGENT — Failure Mode Review

**Agent Role:** Failure mode and recovery specialist
**Date:** 2026-02-07
**Files Reviewed:** All system files — focus on error handling, recovery, long-running stability

---

## FAILURE SCENARIO ANALYSIS

### Scenario 1: ESP32 Crashes/Reboots

| Aspect | Assessment |
|--------|-----------|
| WiFi reconnect | ✓ Handled in setup() — reconnects on boot |
| buddy_vision.py stream | ✓ Reconnects every 2-3s (stream_receiver_thread) |
| buddy_web_full_V2.py WS | ✓ Reconnects via teensy_poll_loop with backoff |
| Teensy impact | ✓ Loses face data, times out gracefully (2s) |
| State consistency | ⚠ Teensy may be mid-tracking when data stops |

**Risk level:** LOW — recovery is handled correctly

---

### Scenario 2: WiFi Drops for 30+ Seconds

| Aspect | Assessment |
|--------|-----------|
| ESP32 reconnect | ✓ Checks every 30s, 5s reconnect timeout |
| Vision pipeline | ✓ Retries every 2s with logging |
| WebSocket commands | ✓ Exponential backoff (3s, 6s, 9s... up to 30s) |
| Teensy autonomous | ✓ Runs behavior engine, ambient life, needs decay |
| Post-reconnect | ⚠ Face tracking resumes, state may be inconsistent |

**Risk level:** LOW-MODERATE — functional but may need manual verification

---

### Scenario 3: Ollama Down or Extremely Slow

## FINDING R5-1: CRITICAL — Ollama Query Can Block Forever

**Severity:** CRITICAL — system becomes completely unresponsive

**Analysis:**

`query_ollama()` (buddy_web_full_V2.py:1056) calls:
```python
return ollama.chat(model=CONFIG["ollama_model"], messages=msgs)["message"]["content"]
```

This call has **no timeout**. The Ollama Python library's `chat()` uses `requests` internally, which by default has no socket timeout.

**Failure timeline:**
1. User speaks to Buddy → `process_input()` starts
2. `is_processing = True` (line 1068)
3. THINKING animation starts on Teensy
4. `query_ollama()` called (line 1094)
5. Ollama is down / GPU is OOM / model is swapping to disk
6. Call hangs indefinitely
7. System is now PERMANENTLY stuck:
   - `is_processing = True` → blocks all new input
   - THINKING animation plays forever on Teensy
   - Wake word triggers are ignored (line 846-847: `if is_processing: continue`)
   - Spontaneous speech is blocked
   - Text input returns immediately (line 1067: `if is_processing: return`)
   - **Only fix: restart the Python process**

**Impact:** Complete system lockup requiring manual intervention. Unacceptable for a system meant to run unattended for hours.

**Fix:**
```python
def query_ollama(text, img=None, timeout=60):
    state_info = get_buddy_state_prompt()
    prompt = CONFIG["system_prompt"].replace("{buddy_state}", state_info)
    msgs = [{"role": "system", "content": prompt}]
    if img:
        msgs.append({"role": "user", "content": text, "images": [img]})
    else:
        msgs.append({"role": "user", "content": text})

    # Run in thread with timeout
    result = [None]
    error = [None]

    def _query():
        try:
            result[0] = ollama.chat(
                model=CONFIG["ollama_model"],
                messages=msgs
            )["message"]["content"]
        except Exception as e:
            error[0] = e

    t = threading.Thread(target=_query, daemon=True)
    t.start()
    t.join(timeout=timeout)

    if t.is_alive():
        raise TimeoutError(f"Ollama timed out after {timeout}s — model may be overloaded")
    if error[0]:
        raise error[0]
    return result[0]
```

---

## FINDING R5-2: SIGNIFICANT — No WebSocket Reconnection After ESP32 Reboot

**Severity:** SIGNIFICANT — WebSocket connection not restored after ESP32 reboot

**Analysis:**

When the ESP32 reboots:
1. WebSocket connection drops
2. `teensy_send_ws()` catches the exception (line 664-666):
```python
except Exception as e:
    teensy_connected = False
    socketio.emit('log', {'message': f'WebSocket error: {e}', 'level': 'error'})
    return None
```
3. `teensy_poll_loop()` detects `teensy_connected == False` (line 728-730)
4. Calls `connect_teensy()` which tries WebSocket first

**But:** The reconnection logic in `teensy_poll_loop()` (lines 727-739) has an issue:
```python
if s:  # query_teensy_state() returned data
    ws_reconnect_count = 0
else:
    teensy_connected = False
    ws_reconnect_count += 1
    # ...
    time.sleep(wait)
    connect_teensy()
```

The problem is that `query_teensy_state()` → `teensy_send_command("QUERY")` → `teensy_send_ws("QUERY")` checks `if not teensy_connected: return None` (line 641). If `teensy_connected` was set to False by a previous WebSocket error, `query_teensy_state()` returns None WITHOUT trying to reconnect. The reconnect only happens in the outer `else` branch.

But `teensy_connected = False` is set in `teensy_send_ws()` (line 665), then `teensy_poll_loop()` sees `teensy_connected == False` in the outer `if teensy_connected:` check (line 718) and goes to the `else` branch:
```python
else:
    connect_teensy()
```

This IS handled correctly. The reconnect happens in the else branch when `teensy_connected` is False. My initial concern was wrong.

**However,** there's still a timing issue: after ESP32 reboots, the WebSocket server may not be immediately available. `connect_teensy_ws()` (line 583-584) has a 5-second timeout:
```python
ws_connection = websocket.create_connection(url, timeout=5)
```

If the ESP32 takes longer than 5 seconds to boot and start its WebSocket server, the connection attempt fails and falls back to USB serial. Subsequent poll cycles will try WebSocket again, but the fallback to serial overwrites the comm mode:
```python
CONFIG["teensy_comm_mode"] = "serial"  # line 617 — permanent switch!
```

**Impact:** After ESP32 reboot, the system permanently switches to USB serial mode and never retries WebSocket.

**Fix:** Don't permanently override the comm mode on fallback:
```python
def connect_teensy_serial():
    # ...
    # CONFIG["teensy_comm_mode"] = "serial"  # REMOVE THIS LINE
    # Instead, just set a temporary flag:
    global using_serial_fallback
    using_serial_fallback = True
```

---

## FINDING R5-3: SIGNIFICANT — Teensy EEPROM State Could Be Corrupted on Power Loss

**Severity:** SIGNIFICANT — personality/learning state could be corrupted

**Analysis:**

The main loop saves state every 30 minutes (Buddy_VersionflxV18.ino:831-835):
```cpp
static unsigned long lastSave = 0;
if (now - lastSave > 1800000) {
    behaviorEngine.saveState();
    lastSave = now;
}
```

If power is lost during `saveState()` (which writes to EEPROM), partial writes could corrupt the stored personality, learning weights, or spatial memory.

**Impact:** After power restoration, the behavior engine could start with corrupted state:
- Personality traits at extreme values
- Learning weights that cause bizarre behavior selection
- Spatial memory with invalid grid values

**Fix:** Use a write-verify pattern:
```cpp
void saveState() {
    // Write to secondary EEPROM region first
    writeStateToRegion(BACKUP_REGION);
    // Verify backup
    if (verifyStateAtRegion(BACKUP_REGION)) {
        // Copy to primary region
        writeStateToRegion(PRIMARY_REGION);
        // Set valid flag
        EEPROM.write(VALID_FLAG_ADDR, 0xAA);
    }
}

void loadState() {
    if (EEPROM.read(VALID_FLAG_ADDR) == 0xAA) {
        loadStateFromRegion(PRIMARY_REGION);
    } else {
        // Primary corrupt, try backup
        loadStateFromRegion(BACKUP_REGION);
    }
}
```

---

## FINDING R5-4: MODERATE — buddy_vision.py Crash Leaves Face Tracking in Stale State

**Severity:** MODERATE — tracking continues with stale data briefly

**Analysis:**

If buddy_vision.py crashes:
1. No more UDP face data sent to ESP32
2. ESP32 (old firmware) continues its own local detection — no impact
3. buddy_web_full_V2.py's `get_vision_state()` returns None (line 976-978):
```python
except:
    pass
return None
```
4. `check_vision_pipeline()` reports offline
5. `capture_frame()` falls back to ESP32 `/capture`

**However:** If the system is running on the new architecture (once Package 1 exists), UDP face data would stop entirely. The Teensy's `reflexController.checkTimeout()` would disable tracking after 2 seconds (ReflexiveControl.h:438-443):
```cpp
if (timeSinceFace > 2000) {
    state.active = false;
    state.shouldBeActive = false;
}
```

This is correct behavior.

**Impact:** 2-second delay before tracking disengages. During this time, servos hold their last position. Not dangerous, but Buddy appears to "freeze" briefly.

**Fix:** No code change needed. The 2-second timeout is appropriate. Consider adding a log message when vision is lost:
```python
# In teensy_poll_loop, periodically check vision:
if not get_vision_state():
    socketio.emit('log', {'message': 'Vision pipeline offline', 'level': 'warning'})
```

---

## FINDING R5-5: MODERATE — TTS Temp Files Could Accumulate on Crash

**Severity:** MODERATE — disk space leak over time

**Analysis:**

`generate_tts()` (buddy_web_full_V2.py:1058-1063):
```python
async def generate_tts(text):
    with tempfile.NamedTemporaryFile(suffix=".mp3", delete=False) as f:
        tp = f.name
    try:
        await edge_tts.Communicate(...).save(tp)
        with open(tp, "rb") as f:
            return base64.b64encode(f.read()).decode("utf-8")
    finally:
        os.unlink(tp)
```

The `finally` block handles normal cleanup. But if the process is killed (SIGKILL, OOM killer) during TTS generation, the temp file is left behind. Over weeks of operation, these could accumulate.

Similarly, `record_and_process()` creates temp WAV files (line 887-891) with `finally: os.unlink(wp)`.

**Impact:** Minor disk space leak. Each file is small (~50KB for TTS, ~200KB for recordings).

**Fix:** Add startup cleanup:
```python
import glob

def cleanup_temp_files():
    """Remove stale temp files from previous runs."""
    patterns = [
        os.path.join(tempfile.gettempdir(), "tmp*.mp3"),
        os.path.join(tempfile.gettempdir(), "tmp*.wav"),
        os.path.join(tempfile.gettempdir(), "tmp*.webm"),
    ]
    for pattern in patterns:
        for f in glob.glob(pattern):
            try:
                # Only delete if older than 1 hour
                if time.time() - os.path.getmtime(f) > 3600:
                    os.unlink(f)
            except:
                pass
```

---

## FINDING R5-6: MODERATE — start_buddy.py Has No ESP32 Boot Wait

**Severity:** MODERATE — race condition on startup

**Analysis:**

`start_buddy.py` (lines 52-67):
```python
# Start vision pipeline
p_vision = subprocess.Popen(vision_cmd)
time.sleep(3)  # Let vision pipeline connect to stream

# Start main server
p_server = subprocess.Popen(server_cmd)
```

The 3-second delay assumes the ESP32 is already booted and the stream is available. If the ESP32 powers on at the same time as the PC (e.g., after a power outage):
1. ESP32 boot time: 3-10 seconds (WiFi connection)
2. buddy_vision.py starts at T+0
3. Tries to connect to stream at T+0 — fails (ESP32 still booting)
4. Retries every 3s (buddy_vision.py:268-269)
5. buddy_web_full_V2.py starts at T+3
6. Tries WebSocket at T+3 — may also fail

Both scripts DO have retry logic, so they eventually connect. But the user sees error messages during startup.

**Fix:** Add ESP32 readiness check in start_buddy.py:
```python
import requests

def wait_for_esp32(ip, timeout=30):
    """Wait for ESP32 to be ready."""
    print(f"[LAUNCHER] Waiting for ESP32 at {ip}...")
    start = time.time()
    while time.time() - start < timeout:
        try:
            r = requests.get(f"http://{ip}/health", timeout=2)
            if r.status_code == 200:
                print(f"[LAUNCHER] ESP32 ready!")
                return True
        except:
            pass
        time.sleep(1)
    print(f"[LAUNCHER] ESP32 not ready after {timeout}s, starting anyway...")
    return False
```

---

## FINDING R5-7: LOW — Browser Audio Context May Block After Reconnect

**Severity:** LOW — requires user interaction to resume

**Analysis:**

Modern browsers require a user gesture (click, keypress) to allow audio playback. The SocketIO client handles reconnection automatically, but after a disconnect/reconnect:

1. Socket.IO reconnects
2. Server sends `audio` event with TTS audio
3. Browser's `audioPlayer.play()` (line 439) may fail silently if the page hasn't received a user gesture since the reconnection

**Impact:** Buddy responds with text and movements but no audio after a reconnection, until the user clicks somewhere on the page.

**Fix:** Add a user notification:
```javascript
audioPlayer.play().catch(e => {
    log('Audio blocked — click anywhere to enable', 'warning');
    document.addEventListener('click', () => {
        audioPlayer.play();
    }, {once: true});
});
```

---

## FINDING R5-8: LOW — No Heartbeat Between Systems

**Severity:** LOW — outages detected reactively, not proactively

**Analysis:**

There is no heartbeat mechanism between:
- buddy_vision.py ↔ ESP32 (stream health only)
- buddy_web_full_V2.py ↔ buddy_vision.py (polled on-demand)
- buddy_web_full_V2.py ↔ ESP32/Teensy (1s poll interval)

Outages are detected when a request fails, not proactively. This means:
- If buddy_vision.py hangs (not crashes), `get_vision_state()` will time out after 1 second
- If the ESP32's WiFi is flaky, each poll has a 500ms timeout

**Impact:** Minimal — the polling approach works well enough for the 1-second update interval.

---

## SUMMARY

| Finding | Severity | Recovery? |
|---------|----------|----------|
| R5-1: Ollama query blocks forever | CRITICAL | NO — requires process restart |
| R5-2: WebSocket mode permanently lost | SIGNIFICANT | Semi — stuck on serial |
| R5-3: EEPROM corruption on power loss | SIGNIFICANT | Partial — defaults to initial |
| R5-4: Vision crash leaves stale tracking | MODERATE | YES — 2s timeout |
| R5-5: Temp files accumulate | MODERATE | Needs cleanup |
| R5-6: No ESP32 boot wait | MODERATE | YES — retry logic exists |
| R5-7: Browser audio blocked | LOW | YES — user click |
| R5-8: No heartbeat | LOW | Acceptable |

**Overall robustness assessment:** The system has GOOD recovery for network/hardware failures but POOR handling of software hangs (especially Ollama). The Teensy's autonomous behavior system provides excellent degraded-mode operation when connectivity is lost.
