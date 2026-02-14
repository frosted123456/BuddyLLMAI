# Buddy Voice Assistant

Flask/SocketIO server for a physical desk robot. Python 3, multi-threaded.

## Files
- `buddy_web_full_V2.py` — main server (~2000 lines)
- `narrative_engine.py` — personality, salience, scene context
- `intent_manager.py` — intent classification
- `salience_filter.py` — relevance scoring
- `physical_expression.py` — Teensy animation mapping

## Critical Threading Info
Key locks (ALL must use try/finally):
- `processing_lock` — gates all speech generation
- `_ollama_lock` — prevents LLM contention
- `ws_lock` — WebSocket to ESP32
- `teensy_state_lock` — Teensy state
- `_pending_speech_lock` — delayed speech

Rule: never hold locks across blocking I/O. Never nest locks.

## External Services
- Ollama localhost:11434 (slow, 10-60s)
- Whisper base (local STT)
- edge-tts (needs internet)
- ESP32 WebSocket (unreliable)

## Run
`python buddy_web_full_V2.py` → browser localhost:5000

## Known Bugs
- Gets stuck in "Transcribing..." requiring restart
- Wake word crash: access violation on porcupine (race condition, no lock)
- Lock leaks suspected on exception paths
