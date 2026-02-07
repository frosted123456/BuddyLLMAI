# Buddy Robot

Buddy is a desk companion robot with a 3-DOF head (pan/nod/tilt), camera-based vision, and an AI personality driven by an 8-layer behavior architecture. It tracks faces, holds conversations, expresses emotions through physical movement, and develops a persistent personality over time.

The system runs across three devices: an ESP32-S3 handles the camera and WiFi bridge, a Teensy 4.0 runs the behavior engine and servos, and a Server PC runs all AI processing (vision, speech, language model).

[Photo of Buddy here]

**Key features:**

- Real-time face tracking with adaptive PID control (50Hz servo loop)
- Conversational AI with vision context (Ollama LLaVA + Whisper + Edge TTS)
- 8-layer behavior architecture: needs, personality, relationships, perception, social modeling, emotion, behavior selection, body schema
- Personality that drifts slowly based on interactions (persisted to EEPROM)
- Episodic memory, spatial awareness, and goal formation
- Web UI accessible from any browser on the network
- Wake word detection ("Jarvis") or push-to-talk

**Architecture:**

```
                                WiFi                             Web browser
┌──────────────┐     ──────────────────     ┌──────────────┐     ──────────     ┌──────────────┐
│  ESP32-S3    │◄──────────────────────────►│  Server PC   │◄─────────────────►│  Office PC   │
│  Camera +    │  MJPEG stream (/stream)    │  (RTX 3080)  │  http://:5000     │  (browser)   │
│  WiFi Bridge │  UDP face data (:8888)     │              │                   │              │
│              │  WebSocket cmds (ws://:81) │  Vision AI   │                   │              │
└──────┬───────┘                            │  Speech AI   │                   └──────────────┘
       │ UART 921600 baud                   │  LLM (Ollama)│
       │                                    └──────────────┘
┌──────┴───────┐
│  Teensy 4.0  │
│  Behavior    │
│  Engine      │
│  3 Servos    │
│  Ultrasonic  │
│  Buzzer      │
└──────────────┘
```

---

## Table of Contents

1. [Quick Start](#1-quick-start)
2. [Hardware](#2-hardware)
3. [Software Installation](#3-software-installation)
4. [Configuration](#4-configuration)
5. [Running the System](#5-running-the-system)
6. [Quick Reference](#6-quick-reference)
7. [Architecture Overview](#7-architecture-overview)
8. [File Structure](#8-file-structure)
9. [Troubleshooting](#9-troubleshooting)
10. [Development Notes](#10-development-notes)

---

## 1. Quick Start

For experienced makers who want to get Buddy running fast. See the detailed sections below if anything is unclear.

```
1. Flash ESP32-S3:   Edit WiFi credentials in Buddy_ESP32_Bridge.ino, upload
2. Flash Teensy 4.0: Upload Buddy_VersionflxV18.ino (no edits needed)
3. Wire UART:        Teensy RX1(pin 0) ← ESP32 TX(GPIO 43)
                     Teensy TX1(pin 1) → ESP32 RX(GPIO 44)
                     Common GND. Separate USB power for each.
4. Wire servos:      Pins 2, 3, 4 on Teensy. Dedicated 5V 3A supply for servos.
5. Install Python:   pip install flask flask-socketio ollama openai-whisper edge-tts
                     pip install opencv-python mediapipe numpy requests pillow
                     pip install pyserial websocket-client
6. Pull LLM:         ollama pull llava
7. Edit config:      Set esp32_ip in buddy_web_full_V2.py CONFIG dict
8. Launch:           python start_buddy.py --esp32-ip <ESP32_IP> --rotate 90
9. Open browser:     http://localhost:5000
```

---

## 2. Hardware

### 2A. Bill of Materials

| Component | Specific Part | Qty | Notes |
|-----------|--------------|-----|-------|
| Microcontroller | Teensy 4.0 | 1 | 600MHz ARM Cortex-M7, behavior engine |
| Camera module | Freenove ESP32-S3 WROOM CAM Board | 1 | OV2640 camera, 8MB PSRAM |
| Servos | Goteck GS-9025MG or equiv metal gear | 3 | Quiet, 180 deg range |
| Ultrasonic sensor | HC-SR04 | 1 | Distance sensing |
| Buzzer | Passive piezo buzzer | 1 | Audio feedback (droid speak) |
| Servo power supply | 5V 3A USB-C or barrel jack | 1 | Dedicated for servos |
| USB cable (Teensy) | USB-A to Micro-USB | 1 | Programming + serial |
| USB cable (ESP32) | USB-C | 1 | Programming + power |
| Jumper wires | Male-to-male | ~10 | UART + sensors |
| Server PC | Any PC with NVIDIA GPU | 1 | RTX 3060+ recommended |
| Office PC | Any PC/tablet with browser | 1 | Optional, for remote web UI |

### 2B. Wiring Diagram

```
TEENSY 4.0                           ESP32-S3 WROOM CAM
──────────                           ────────────────────
Pin 0 (RX1) ◄─────────────────────── GPIO 43 (TX)
Pin 1 (TX1) ──────────────────────►── GPIO 44 (RX)
GND ───────────────────────────────── GND
                                      (5V from its own USB — DO NOT share power)

Pin 2  → baseServo signal (white/orange wire)
Pin 3  → nodServo signal
Pin 4  → tiltServo signal

Pin 14 → HC-SR04 Echo
Pin 15 → HC-SR04 Trig

Pin 10 → Buzzer signal

Servo power: 5V 3A from dedicated supply (NOT from Teensy 5V pin)
Servo GND:   Common ground with Teensy GND
```

**WARNING: The ESP32 MUST have its own USB power supply. Do NOT power it from Teensy or the servo supply. Three servos moving simultaneously draw ~1.5A and will brownout the ESP32 if they share power.**

**WARNING: UART cross-connect: Teensy TX → ESP32 RX, Teensy RX ← ESP32 TX. Reversing these means no communication.**

### 2C. Servo Ranges

These ranges are enforced in firmware. Commanding values outside them has no effect.

| Servo | Teensy Pin | Min | Max | Center | Purpose |
|-------|-----------|-----|-----|--------|---------|
| baseServo | 2 | 10 deg | 170 deg | 90 deg | Rotate head left/right |
| nodServo | 3 | 80 deg | 150 deg | 115 deg | Tilt head up/down |
| tiltServo | 4 | 20 deg | 150 deg | 85 deg | Head lean/tilt |

---

## 3. Software Installation

### 3A. Arduino IDE Setup

#### Teensy 4.0

```
1. Install Arduino IDE 2.x
   https://www.arduino.cc/en/software

2. Install Teensyduino
   https://www.pjrc.com/teensy/td_download.html

3. Arduino IDE settings:
   Board:       "Teensy 4.0"
   USB Type:    "Serial"
   CPU Speed:   "600 MHz"

4. Open Buddy_VersionflxV18/Buddy_VersionflxV18.ino

5. Verify it compiles (Ctrl+R)
   Do NOT upload yet — wire the hardware first.
```

Verify: Sketch compiles with 0 errors. Warnings are expected.

#### ESP32-S3

```
1. In Arduino IDE: File → Preferences → Additional Board Manager URLs:
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json

2. Tools → Board Manager → Search "esp32" → Install "esp32 by Espressif" (2.0.14+)

3. Install library:
   Sketch → Include Library → Manage Libraries →
   Search "WebSockets" → Install "WebSockets by Markus Sattler" (2.4.0+)

4. Board settings — EVERY SETTING MATTERS:
   Board:              "ESP32S3 Dev Module"
   USB CDC On Boot:    "Enabled"             ← CRITICAL
   CPU Frequency:      "240MHz (WiFi)"
   PSRAM:              "OPI PSRAM"           ← CRITICAL (NOT "QSPI")
   Partition Scheme:   "Huge APP (3MB No OTA/1MB SPIFFS)"
   Flash Size:         "16MB (128Mb)"

5. Edit Buddy_ESP32_Bridge/Buddy_ESP32_Bridge.ino lines 45-46:
   const char* WIFI_SSID = "YOUR_WIFI_SSID";
   const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

6. Upload (make sure the correct USB port is selected)

7. Open Serial Monitor at 921600 baud
   You should see:
     [WIFI] Connected! IP: 192.168.x.x
     [READY] Bridge active
```

Verify: Serial monitor shows the IP address. Open `http://<that IP>/stream` in a browser — you should see live video.

**WARNING: Wrong PSRAM setting ("QSPI" instead of "OPI") causes boot loops. Wrong "USB CDC On Boot" setting causes no serial output.**

### 3B. Server PC Setup (Python)

**Prerequisites:**

- Windows 10/11 (64-bit) or Linux
- NVIDIA GPU with recent drivers
- Python 3.10 or 3.11 (**NOT** 3.12+ due to MediaPipe compatibility)

**Step-by-step:**

```
1. Install Python 3.11:
   Download from https://python.org
   On Windows: check "Add Python to PATH" during install

   Verify:
   python --version
   # Expected: Python 3.11.x
```

```
2. Install CUDA Toolkit (for GPU-accelerated Whisper):
   Download from https://developer.nvidia.com/cuda-downloads
   Full install, default options

   Verify:
   nvidia-smi
   # Should show GPU info and driver version
```

```
3. Install FFmpeg (required by Whisper):
   Windows:
     Download from https://gyan.dev/ffmpeg/builds (essentials zip)
     Extract to C:\ffmpeg
     Add C:\ffmpeg\bin to system PATH
   Linux:
     sudo apt install ffmpeg

   Verify:
   ffmpeg -version
```

```
4. Install Ollama:
   Download from https://ollama.com/download
   After install, pull the vision model:
   ollama pull llava

   Verify:
   ollama list
   # Should show llava in the list
```

```
5. Install Python packages:
   pip install flask flask-socketio requests Pillow pyserial
   pip install opencv-python mediapipe numpy
   pip install ollama openai-whisper edge-tts
   pip install websocket-client

   For CUDA-accelerated PyTorch (recommended):
   pip install torch torchvision torchaudio --index-url https://download.pytorch.org/whl/cu121

   Optional (wake word detection):
   pip install pvporcupine pvrecorder

   Verify:
   python -c "import cv2; print('OpenCV:', cv2.__version__)"
   python -c "import mediapipe; print('MediaPipe OK')"
   python -c "import whisper; print('Whisper OK')"
   python -c "import ollama; print('Ollama OK')"
   python -c "import flask; print('Flask OK')"
```

**Common pip install failures:**

| Error | Fix |
|-------|-----|
| `MediaPipe "Python 3.12 not supported"` | Use Python 3.10 or 3.11 |
| `whisper build fails` | Run `pip install setuptools wheel` first |
| `torch CUDA not found` after import | Use the `--index-url` flag for cu121 wheels |
| `pvporcupine fails to install` | Optional — skip it, push-to-talk works without wake word |
| `No module named serial` | Run `pip install pyserial` (not `pip install serial`) |

---

## 4. Configuration

### 4A. ESP32 Bridge (edit before flashing)

In `Buddy_ESP32_Bridge/Buddy_ESP32_Bridge.ino`, lines 45-46:

```cpp
const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
```

No other ESP32 config changes are needed for normal operation.

### 4B. Python Server

In `buddy_web_full_V2.py`, the `CONFIG` dict starting at line 55:

```python
CONFIG = {
    "esp32_ip": "192.168.x.x",        # IP from ESP32 serial monitor
    "esp32_ws_port": 81,               # WebSocket port (default: 81)
    "teensy_comm_mode": "websocket",   # "websocket" (via ESP32) or "serial" (USB)

    "vision_api_url": "http://localhost:5555",  # buddy_vision.py API

    "ollama_model": "llava",           # Must match: ollama pull llava
    "ollama_host": "http://localhost:11434",

    "whisper_model": "base",           # "tiny", "base", "small", "medium"
    "tts_voice": "en-US-GuyNeural",    # Edge TTS voice name
    "tts_rate": "+10%",

    "teensy_port": "COM12",            # Only used in "serial" mode
    "teensy_baud": 115200,             # USB serial baud to Teensy

    "picovoice_access_key": "",        # From env var or picovoice.ai
    "wake_word": "jarvis",
}
```

The minimum change required is `esp32_ip`. Set it to the IP address shown in the ESP32 serial monitor.

### 4C. Environment Variables (optional)

For wake word detection without hardcoding the key:

```bash
# Windows
set PICOVOICE_ACCESS_KEY=your_key_here

# Linux / macOS
export PICOVOICE_ACCESS_KEY=your_key_here
```

Get a free key at [picovoice.ai](https://picovoice.ai). Wake word is optional — push-to-talk works without it.

---

## 5. Running the System

### 5A. Startup Order (this matters)

```
Step 1: Power on Buddy hardware
  - Connect ESP32-S3 via USB (its own power source)
  - Connect Teensy via USB to Server PC (or its own power source)
  - Wait 5 seconds for ESP32 WiFi connection

Step 2: Verify ESP32 is streaming
  - Open browser: http://<ESP32_IP>/stream
  - You should see live camera video
  - If not: check WiFi credentials, check USB power

Step 3: Start Buddy (all-in-one launcher)
  cd /path/to/BuddyLLMAI
  python start_buddy.py --esp32-ip <ESP32_IP> --rotate 90

  This starts both buddy_vision.py and buddy_web_full_V2.py.
  Wait for: [LAUNCHER] All services running.

Step 4: Open browser
  http://localhost:5000          (from server PC)
  http://<SERVER_IP>:5000        (from office PC on same network)
```

**Alternative: Start services separately**

If you need more control or want to see individual logs:

```
# Terminal 1: Vision pipeline
python buddy_vision.py --esp32-ip <ESP32_IP> --rotate 90
# Wait for: [STREAM] Connected!

# Terminal 2: Main server
python buddy_web_full_V2.py
# Wait for output indicating server is ready

# Terminal 3: Browser
# Open http://localhost:5000
```

### 5B. Shutdown Order

```
1. Close browser tab
2. Ctrl+C in the start_buddy.py terminal (or Ctrl+C in each terminal)
3. Power off Buddy hardware (optional)
```

### 5C. Quick-Start Batch File (Windows)

Save as `start.bat` in the project root:

```batch
@echo off
echo Starting Buddy...
cd /d %~dp0
start "Buddy Vision" python buddy_vision.py --esp32-ip 192.168.1.100 --rotate 90
timeout /t 5 /nobreak >nul
start "Buddy Server" python buddy_web_full_V2.py
echo.
echo Buddy is running. Open http://localhost:5000
pause
```

Edit the IP address to match your ESP32.

### 5D. Quick-Start Shell Script (Linux)

Save as `start.sh` in the project root:

```bash
#!/bin/bash
echo "Starting Buddy..."
cd "$(dirname "$0")"
python3 buddy_vision.py --esp32-ip 192.168.1.100 --rotate 90 &
VISION_PID=$!
sleep 5
python3 buddy_web_full_V2.py &
SERVER_PID=$!
echo ""
echo "Buddy is running. Open http://localhost:5000"
echo "Press Ctrl+C to stop."
trap "kill $VISION_PID $SERVER_PID 2>/dev/null; exit" INT TERM
wait
```

---

## 6. Quick Reference

### Endpoints and Ports

| Service | URL / Address | Notes |
|---------|---------------|-------|
| ESP32 MJPEG Stream | `http://<ESP32_IP>/stream` | Live video |
| ESP32 Snapshot | `http://<ESP32_IP>/capture` | Single JPEG frame |
| ESP32 Health | `http://<ESP32_IP>/health` | Heap, PSRAM, uptime, counters |
| Vision API State | `http://localhost:5555/state` | Face tracking + rich analysis JSON |
| Vision API Snapshot | `http://localhost:5555/snapshot` | Annotated JPEG frame |
| Web UI | `http://localhost:5000` | Main Buddy interface |
| Ollama LLM | `http://localhost:11434` | LLM API (auto-started by Ollama) |
| ESP32 WebSocket | `ws://<ESP32_IP>:81` | Command bridge (PC to Teensy) |
| ESP32 UDP | `<ESP32_IP>:8888` | Face data fast path (PC to Teensy) |
| ESP32 to Teensy UART | 921600 baud | Serial1 on both devices |
| PC to Teensy USB | 115200 baud | Fallback serial (teensy_comm_mode: "serial") |

### AI Bridge Commands

Commands sent from the PC to Teensy via the ESP32 WebSocket bridge. All prefixed with `!`.

| Command | Description |
|---------|-------------|
| `!QUERY` | Returns full state JSON (emotion, needs, behavior, servos) |
| `!LOOK:base,nod` | Move servos to position (blocked during reflex tracking) |
| `!SATISFY:need,amount` | Satisfy a need: social, stimulation, novelty (0.0-1.0) |
| `!PRESENCE` | Simulate human presence detection |
| `!EXPRESS:emotion` | Express emotion: curious, excited, content, anxious, neutral, startled, bored, confused |
| `!NOD:count` | Nod yes animation (1-10) |
| `!SHAKE:count` | Shake no animation (1-10) |
| `!STREAM:on/off` | Toggle periodic state broadcast |
| `!ATTENTION:dir` | Look direction: center, left, right, up, down |
| `!LISTENING` | Attentive pose for wake-word detection |
| `!THINKING` | Start looping pondering animation (non-blocking) |
| `!STOP_THINKING` | Stop thinking animation |
| `!SPEAKING` | Start looping conversational animation (non-blocking) |
| `!STOP_SPEAKING` | Stop speaking animation |
| `!ACKNOWLEDGE` | Quick subtle nod |
| `!CELEBRATE` | Happy bounce animation |
| `!IDLE` | Clear AI state, return to autonomous behavior |
| `!SPOKE` | Acknowledge spontaneous speech completed |
| `!VISION:json` | Feed PC vision observations into behavior engine |

---

## 7. Architecture Overview

### Three-Device System

**ESP32-S3: Camera + WiFi Bridge** (no AI processing)
- Streams MJPEG video to Server PC at 15 FPS (640x480)
- Relays AI commands between PC (WebSocket) and Teensy (UART)
- Forwards face coordinates from PC (UDP) to Teensy (UART)
- Dual-core: Core 0 handles HTTP/stream, Core 1 handles WebSocket/UDP
- UART mutex prevents interleaving of face data and commands

**Server PC: The Brain**
- MediaPipe face detection at 30 FPS with velocity tracking
- Rich vision analysis: expressions, object count, scene novelty, movement
- Ollama LLaVA for vision-aware conversation
- OpenAI Whisper for speech-to-text
- Edge TTS for text-to-speech (streamed to browser)
- Flask web UI with SocketIO for real-time state updates
- Optional Porcupine wake word detection ("Jarvis")

**Teensy 4.0: The Body**
- 8-layer behavior architecture running at 50Hz
- 3-DOF servo head control with emotion-driven movement styles
- Reflexive face tracking with adaptive PID controller
- Autonomous behaviors: explore, idle, play, social engage, investigate, retreat, rest, vigilant
- Persistent personality and learning (EEPROM)
- Ultrasonic distance sensing
- Piezo buzzer for droid-speak audio feedback

### Behavior Layers

The Teensy runs an 8-layer behavior system. Each layer is a separate `.h` module:

| Layer | Module | Description |
|-------|--------|-------------|
| 1. Homeostatic Needs | `Needs.h` | Internal drives (social, stimulation, energy, safety, novelty) that build up over time |
| 2. Personality | `Personality.h` | Seven stable traits (curiosity, caution, sociability, playfulness, excitability, persistence, expressiveness) that drift slowly based on experience |
| 3. Relationships | `BehaviorEngine.h` | Per-person familiarity tracking (stranger → acquaintance → familiar → family) with interaction history |
| 4. Perception | `AttentionSystem.h` | Attention focus, novelty detection, and spatial awareness across an 8-direction environment grid |
| 5. Social Modeling | `IllusionLayer.h` | Pattern detection, imitation signals, and subjective experience modeling |
| 6. Emotion | `Emotion.h` | 3D arousal/valence/dominance model with 8 discrete labels and continuous dynamics |
| 7. Behavior Selection | `BehaviorSelection.h` | Scores candidate behaviors against current needs, personality, and emotion to pick the best action |
| 8. Body Schema | `BodySchema.h` | Spatial reasoning, servo-to-world mapping, and intentional movement planning |

Additional cognitive systems:
- **Consciousness Layer** (`ConsciousnessLayer.h`) — Epistemic states (confident, uncertain, confused, learning, conflicted, wondering), tension tracking, self-awareness
- **Goal Formation** (`GoalFormation.h`) — Multi-step goal planning and pursuit
- **Episodic Memory** (`EpisodicMemory.h`) — Records significant events for later recall
- **Speech Urge** (`SpeechUrge.h`) — Drives spontaneous speech when Buddy has something to say

### Data Flow

```
1. ESP32 captures camera frame → MJPEG stream over WiFi
2. buddy_vision.py receives frame → MediaPipe face detection
3. Face coordinates sent via UDP(:8888) → ESP32 → UART → Teensy
4. Teensy ReflexiveControl moves servos to track face (50Hz PID loop)
5. buddy_vision.py also runs rich analysis (expressions, objects) at 3Hz
6. Rich analysis sent as !VISION command → WebSocket(:81) → ESP32 → UART → Teensy
7. Teensy behavior engine integrates vision data into needs, emotion, consciousness
8. buddy_web_full_V2.py queries Teensy state (!QUERY) for LLM context
9. User speaks → Whisper transcription → Ollama LLaVA generates response
10. Response text → Edge TTS → audio streamed to browser
11. Expression tags in response ([NOD], [CURIOUS], etc.) → commands to Teensy
```

---

## 8. File Structure

| File | Device/Location | Purpose |
|------|----------------|---------|
| `Buddy_ESP32_Bridge/Buddy_ESP32_Bridge.ino` | ESP32-S3 | WiFi bridge firmware: camera stream, WebSocket, UDP |
| `Buddy_VersionflxV18/Buddy_VersionflxV18.ino` | Teensy 4.0 | Main firmware entry point, setup, loop |
| `Buddy_VersionflxV18/LittleBots_Board_Pins.h` | Teensy | Pin definitions (echo, trig, buzzer) |
| `Buddy_VersionflxV18/AIBridge.h` | Teensy | AI command handler (serial protocol) |
| `Buddy_VersionflxV18/ReflexiveControl.h` | Teensy | Face tracking reflexes (adaptive PID) |
| `Buddy_VersionflxV18/BehaviorEngine.h` | Teensy | Master behavior system integration |
| `Buddy_VersionflxV18/BehaviorSelection.h` | Teensy | Behavior scoring and selection |
| `Buddy_VersionflxV18/Personality.h` | Teensy | 7-trait personality with slow drift |
| `Buddy_VersionflxV18/Emotion.h` | Teensy | 3D arousal/valence/dominance model |
| `Buddy_VersionflxV18/Needs.h` | Teensy | Homeostatic drives (social, stimulation, etc.) |
| `Buddy_VersionflxV18/Learning.h` | Teensy | EEPROM persistence for personality drift |
| `Buddy_VersionflxV18/SpatialMemory.h` | Teensy | 8-direction environment grid |
| `Buddy_VersionflxV18/ServoController.h` | Teensy | Servo abstraction with easing curves |
| `Buddy_VersionflxV18/AnimationController.h` | Teensy | Emotion expression animations |
| `Buddy_VersionflxV18/MovementStyle.h` | Teensy | Emotion-driven movement parameters |
| `Buddy_VersionflxV18/MovementExpression.h` | Teensy | Movement expressiveness system |
| `Buddy_VersionflxV18/PoseLibrary.h` | Teensy | Named pose definitions |
| `Buddy_VersionflxV18/BodySchema.h` | Teensy | Spatial reasoning and body model |
| `Buddy_VersionflxV18/AttentionSystem.h` | Teensy | Attention focus and direction |
| `Buddy_VersionflxV18/ScanningSystem.h` | Teensy | Environmental scanning patterns |
| `Buddy_VersionflxV18/EpisodicMemory.h` | Teensy | Significant event recording |
| `Buddy_VersionflxV18/GoalFormation.h` | Teensy | Multi-step goal planning |
| `Buddy_VersionflxV18/OutcomeCalculator.h` | Teensy | Outcome prediction for decisions |
| `Buddy_VersionflxV18/ConsciousnessLayer.h` | Teensy | Epistemic states and self-awareness |
| `Buddy_VersionflxV18/ConsciousnessManifest.h` | Teensy | Consciousness data structures |
| `Buddy_VersionflxV18/IllusionLayer.h` | Teensy | Subjective experience modeling |
| `Buddy_VersionflxV18/AmbientLife.h` | Teensy | Idle micro-movements (breathing) |
| `Buddy_VersionflxV18/droidSpeak.h` | Teensy | Buzzer sound generation |
| `Buddy_VersionflxV18/checkUltrasonic.h` | Teensy | Ultrasonic sensor helper |
| `buddy_vision.py` | Server PC | Vision pipeline: MJPEG ingest, MediaPipe, UDP output |
| `buddy_web_full_V2.py` | Server PC | Main server: Flask web UI, Ollama, Whisper, TTS, Teensy comms |
| `start_buddy.py` | Server PC | Launcher: starts vision + server together |
| `requirements_vision.txt` | Server PC | pip dependencies for buddy_vision.py |
| `buddyesp32cam-main/` | Reference | Legacy ESP32 camera firmware (not used in current architecture) |
| `review/` | Documentation | Code review reports and verification passes |

---

## 9. Troubleshooting

| Symptom | Likely Cause | Fix |
|---------|-------------|-----|
| ESP32 serial shows garbled text | Wrong baud rate in serial monitor | Set serial monitor to 921600 baud |
| ESP32 boot loops | Wrong PSRAM setting | Must be "OPI PSRAM", not "QSPI". Re-flash. |
| ESP32 "camera init failed" | Cold start timing or ribbon cable | Power cycle. If persistent, reseat camera ribbon cable. |
| ESP32 no serial output at all | "USB CDC On Boot" disabled | Set to "Enabled" in board settings, re-flash. |
| ESP32 connects to WiFi but no video | Wrong partition scheme | Must be "Huge APP (3MB No OTA/1MB SPIFFS)". Re-flash. |
| No face tracking | buddy_vision.py not running | Start vision pipeline first (or use start_buddy.py). |
| Servos jitter at rest | Shared power supply or noise | Use dedicated 5V 3A supply for servos. Add capacitor if needed. |
| Buddy doesn't respond to voice | Ollama not running or model missing | Run `ollama list` and check llava is present. Run `ollama pull llava`. |
| `No module named 'mediapipe'` | Wrong Python version | MediaPipe requires Python 3.10 or 3.11, not 3.12+. |
| `No module named 'serial'` | Wrong package name | Run `pip install pyserial`, not `pip install serial`. |
| WebSocket returns `{"ok":false,"reason":"uart_busy"}` | Command sent during face data relay | Normal — the system retries automatically. |
| WebSocket returns `{"ok":false,"reason":"timeout"}` | Teensy not connected or not responding | Check UART wiring. Check Teensy is powered and running. |
| Teensy not detected on USB | Teensyduino not installed | Install Teensyduino from pjrc.com. |
| Edge TTS fails or no audio | No internet connection | Edge TTS requires an active internet connection. |
| Buddy tracks the wrong direction | Camera rotation mismatch | Try `--rotate 0`, `90`, `180`, or `270` in buddy_vision.py. |
| `torch.cuda.is_available()` returns False | Wrong PyTorch install | Reinstall with `--index-url https://download.pytorch.org/whl/cu121`. |
| Whisper transcription is slow | Running on CPU | Install CUDA toolkit + PyTorch with CUDA support. |
| Vision pipeline "connection refused" | ESP32 not on network | Check ESP32 serial for IP. Verify with `http://<IP>/health`. |
| Face data not reaching Teensy | UART wires crossed or disconnected | Verify RX↔TX cross-connect. Check common GND. |
| Personality seems stuck | EEPROM not saving | Personality saves every 30 minutes. Wait or trigger a save. |
| `pvporcupine` import error | Missing access key | Set `PICOVOICE_ACCESS_KEY` env var, or disable wake word (push-to-talk still works). |
| High latency in responses | Ollama model loading | First query loads the model into VRAM. Subsequent queries are faster. |

---

## 10. Development Notes

For people who want to modify or extend the code.

### Teensy Firmware

- The behavior system is modular — each `.h` file is largely independent. You can modify `Emotion.h` without touching `Personality.h`.
- Add new serial commands in `AIBridge.h` → `handleCommand()`. Follow the existing pattern: prefix with `!`, respond with JSON, terminate with newline.
- Add new behaviors in `BehaviorSelection.h`. Each behavior is scored against current needs, personality, and emotion.
- Servo positions are always constrained to safe ranges in `ReflexiveControl.h` and `ServoController.h`.
- EEPROM persistence saves personality drift every 30 minutes via `Learning.h`.
- The main loop runs at 50Hz (20ms interval). Keep per-frame work under 5ms.

### ESP32 Bridge

- The ESP32 firmware is deliberately minimal — all intelligence lives on the PC. It is a transparent bridge.
- Core 0: camera capture + HTTP server (including blocking MJPEG stream).
- Core 1: WebSocket + UDP processing (main loop).
- UART mutex prevents interleaving of UDP face data and WebSocket command responses.
- WiFi power save is disabled (`WiFi.setSleep(false)`) for low latency.

### Python Server

- `buddy_web_full_V2.py` can run in `"serial"` mode (`teensy_comm_mode: "serial"`) for USB-direct testing without WiFi.
- The vision pipeline (`buddy_vision.py`) is a separate process communicating via HTTP API on port 5555.
- Spontaneous speech is controlled by `spontaneous_speech_enabled`, `spontaneous_max_per_hour`, and `spontaneous_min_gap` in CONFIG.
- The system prompt in CONFIG embeds Buddy's current state JSON, so the LLM has full context of emotion, needs, and behavior.

### Adding a New Sensor

1. Define the pin in `LittleBots_Board_Pins.h`.
2. Initialize in `Buddy_VersionflxV18.ino` `setup()`.
3. Read in the main `loop()` or create a dedicated handler.
4. Feed data into the behavior engine via `Needs`, `Emotion`, or `SpatialMemory`.

### Adding a New AI Command

1. Add the command string match in `AIBridge.h` → `handleCommand()`.
2. Implement the handler method (follow existing `cmd*()` pattern).
3. Respond with JSON: `{"ok":true}` on success, `{"ok":false,"reason":"..."}` on failure.
4. The PC sends commands via WebSocket to `ws://<ESP32_IP>:81` — the ESP32 bridge relays them over UART.

---

## License

[Add license information here]
