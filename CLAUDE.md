# Buddy Voice Assistant

## What This Is
Flask/SocketIO server controlling a physical desk robot. Multi-threaded real-time system with wake word detection, STT (Whisper), LLM (Ollama), TTS (edge-tts), vision (ESP32-CAM), and serial/WebSocket communication to Teensy 4.0 microcontroller.

## Architecture
- Main server: `buddy_web_full_V2.py`
- Narrative engine: `narrative_engine.py` (personality, salience, scene awareness)
- Intent manager: `intent_manager.py` (intent classification)
- Salience filter: `salience_filter.py` (relevance scoring)
- Physical expression: `physical_expression.py` (Teensy animation mapping)

## Threading Model (CRITICAL)
6+ concurrent threads sharing mutable globals. Key locks:
- `processing_lock` — gates ALL speech generation
- `_ollama_lock` — prevents LLM contention
- `ws_lock` — WebSocket to ESP32 bridge
- `teensy_state_lock` — Teensy state tracking
- `porcupine_lock` — (MISSING, needs to be added) wake word access

**Rule**: Never hold locks across blocking I/O. Never nest locks without consistent ordering. Always release in finally blocks.

## External Dependencies
- Ollama (localhost:11434) — can be slow 10-60s on CPU
- Whisper base model — local STT
- edge-tts — needs internet, can timeout
- ESP32 WebSocket bridge — can disconnect randomly
- buddy_vision.py (localhost:5555) — face tracking, may be offline

## Build & Run
```bash
python buddy_web_full_V2.py
```
No test suite exists. Test manually by:
1. Opening browser to localhost:5000
2. Triggering wake word "Jarvis" or using push-to-talk button
3. Verifying Buddy responds and returns to Ready state

## Known Bug Patterns
- System can get stuck in "Transcribing..." requiring restart
- Wake word crashes: "access violation reading 0x0000000000000008"
- Threading races on porcupine/recorder objects (no lock)
- Lock leaks on exception paths in process_input()

## Code Style
- Python 3.x, Flask/SocketIO
- Globals for shared state (legacy pattern, don't refactor to classes)
- SocketIO event handlers run on background threads
- TTS uses dedicated asyncio event loop (`_tts_loop`)
