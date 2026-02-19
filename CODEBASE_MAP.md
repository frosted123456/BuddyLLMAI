# Buddy Robot — Codebase Map

Generated 2026-02-19. Read-only analysis of all project source files.

---

## Python — Server & AI Pipeline

### buddy_web_full_V2.py
Purpose: Monolithic Flask/SocketIO server (~4500 lines) — web UI, LLM speech, wake word, Teensy bridge, vision integration, spontaneous behavior.
Key API: `process_input`, `query_ollama`, `run_tts_sync`, `check_spontaneous_speech`, `process_narrative_speech`, `build_narrative_prompt`, `execute_buddy_actions`, `get_buddy_state_prompt`, `record_and_process`, `wake_word_loop`, `teensy_poll_loop`, `connect_teensy_ws`, `teensy_send_command`, `query_teensy_state`, `send_vision_to_teensy`, `capture_frame`, `get_vision_state`
Depends on: flask, flask_socketio, ollama, whisper, edge_tts, pvporcupine, pvrecorder, serial, websocket-client, requests, PIL, narrative_engine, intent_manager, salience_filter, consciousness_substrate, attention_detector, physical_expression
Issues: 11 locks with suspected leak paths on exceptions; stuck "Transcribing..." state (watchdog partially mitigates); wake word porcupine access violation (race condition); `teensy_connected` set without lock (relies on GIL); `bare except:` clauses throughout; Whisper/Ollama timeout threads leak if join fails; 4500-line monolith with inline HTML

### narrative_engine.py
Purpose: Maintains Buddy's continuous inner narrative — utterance history, conversational threads, mood, human responsiveness, object memory, person profiles, cross-session persistence.
Key API: `record_utterance`, `record_response`, `record_ignored`, `record_human_speech_text`, `record_buddy_response`, `get_conversation_messages`, `get_narrative_context`, `update_mood_narrative`, `update_face_state`, `update_object_memory`, `save_memory`, `load_memory`
Depends on: threading, json, collections
Issues: none found

### intent_manager.py
Purpose: Drives Buddy's social goal pursuit — intent selection, strategy escalation, engagement cycle (eager > persistent > give-up > self-occupy > reluctant retry), speech vs. physical expression routing.
Key API: `IntentManager.select_intent`, `set_intent`, `escalate`, `should_escalate`, `should_act`, `get_intent_context_for_llm`, `mark_success`, `mark_failure`, `person_responded`, `person_departed`, `apply_consciousness_bias`; `StrategyTracker.rank_strategies`, `record_attempt`, `record_outcome`; `should_speak_or_physical`
Depends on: threading, collections
Issues: none found

### salience_filter.py
Purpose: Scores scene descriptions (0-5) using keyword matching + optional LLM, converts periodic vision updates to event-driven Teensy updates.
Key API: `score_description`, `score_description_semantic`, `score_with_fallback`, `should_send_vision_update`, `get_filtered_context`
Depends on: threading, re
Issues: none found

### physical_expression.py
Purpose: Defines non-verbal servo expressions (sigh, double-take, pointed-look, etc.) and the speech performance arc (intention > delivery > watching > resolution).
Key API: `PhysicalExpressionManager.select_expression`, `get_expression_commands`, `get_pre_speech_arc`, `get_post_speech_arc`, `get_resolution_arc`; `calculate_speech_delay`; `PHYSICAL_EXPRESSIONS` dict
Depends on: threading
Issues: none found

### consciousness_substrate.py
Purpose: Cumulative experience layer — somatic state (tension/warmth/restlessness), slow personality drift (trust/openness/resilience), anticipatory model (person patterns > predictions > surprise), vector-backed experience storage.
Key API: `ConsciousnessSubstrate.record_experience`, `get_behavioral_bias`, `get_somatic_influence`, `get_felt_sense`, `start`, `stop`, `save`, `load`; `VectorStore.add`, `search`; `AnticipatoryModel.predict`, `consume_surprise`
Depends on: threading, json, hashlib, ollama (optional for embeddings)
Issues: none found

### attention_detector.py
Purpose: Detects when a person is facing Buddy using rolling-window accumulation of facing_camera signals, with freeze-during-movement and debounce. Includes Silero VAD with amplitude fallback.
Key API: `AttentionDetector.update`, `is_attentive`, `can_trigger_listen`, `freeze`, `unfreeze`; `VoiceActivityDetector.process_frame`, `is_speech`, `is_ready`
Depends on: threading, torch (optional for Silero VAD)
Issues: `VoiceActivityDetector.reset()` acquires `_model_lock` then re-acquires `self.lock` inside — technically safe but could deadlock if lock ordering changes

### buddy_vision.py
Purpose: Separate process — receives ESP32 MJPEG stream, runs MediaPipe face detection (30 FPS) and face mesh analysis (3 FPS), sends face coordinates to ESP32 via UDP, serves vision state API on port 5555.
Key API: `face_tracking_thread`, `rich_vision_thread`, `stream_receiver_thread`, `estimate_head_pose`, `estimate_expression`, `map_to_teensy_coords`; Flask API: `/state`, `/face`, `/health`, `/snapshot`, `/stream`, `/response_detection`
Depends on: opencv-python, mediapipe, numpy, flask, socket (UDP)
Issues: none found

### start_buddy.py
Purpose: Launcher that starts buddy_vision.py and buddy_web_full_V2.py as subprocesses with graceful shutdown.
Key API: `main`, `cleanup`
Depends on: subprocess, signal
Issues: none found

---

## Teensy 4.0 Firmware (Buddy_VersionflxV18/)

### Buddy_VersionflxV18.ino
Purpose: Main sketch — 50Hz loop orchestrating ESP32 serial, servo control, behavior engine, face tracking, and AI bridge command handling.
Key API: `setup`, `loop`, `serialEvent`, `handleFaceDetection`, `parseVisionData`, `moveTo`, `startupAnimation`
Depends on: Servo.h, all project .h files
Issues: `smoothMoveTo` is blocking (uses delay()); `executePlay`/`executeVigilant` block with delay(); `pulseIn` blocks 30ms; serialEvent buffer overflow risk

### AIBridge.h
Purpose: Serial command bridge — parses !COMMANDS from Python host, controls servos/emotions/animations, returns JSON state.
Key API: `init`, `handleCommand`, `updateStreaming`, `cmdVision`, `isAIAnimating`
Depends on: BehaviorEngine.h, ServoController.h, AnimationController.h, ReflexiveControl.h
Issues: `nudge()` parameter order inconsistency between 2-arg and 3-arg overloads; `cmdAcknowledge` uses blocking delay(120); fragile JSON parsing (no tokenizer); 600-byte snprintf buffer is tight

### Emotion.h
Purpose: 3D continuous emotion model (arousal, valence, dominance) with momentum, baseline drift, and discrete emotion labeling.
Key API: `update`, `nudge` (2 overloads), `getLabel`, `getArousal`, `getValence`, `getDominance`
Depends on: Needs.h, Personality.h
Issues: `nudge()` 2-arg=(valence,arousal) vs 3-arg=(arousal,valence,dominance) — swapped parameter order is a latent bug source

### Needs.h
Purpose: Homeostatic needs system — stimulation, social, energy, safety, novelty, expression with decay and satisfaction mechanics.
Key API: `update`, `satisfyStimulation`, `satisfySocial`, `detectHumanPresence`, `detectThreat`, getters for all needs and pressures
Depends on: Personality.h, SpatialMemory.h
Issues: Energy never depletes autonomously (`energyCostRate` starts at 0.0); stale comment on `feelsThreatened` threshold

### Personality.h
Purpose: Seven stable personality traits (curiosity, caution, sociability, playfulness, excitability, persistence, expressiveness) with slow drift from learning.
Key API: `drift`, `getEffectiveCuriosity`, `getEffectiveSociability`, `setArchetype`, trait getters/setters
Depends on: Learning (forward-declared)
Issues: `drift()` defined in Learning.h — fragile cross-file coupling

### BehaviorEngine.h
Purpose: Central orchestrator (~1880 lines) — integrates all subsystems, drives behavior loop at multiple update rates (fast/medium/slow).
Key API: `begin`, `update`, `handlePersonDetection`, `startFaceTracking`, `updateFaceTracking`, `performFaceTracking`, `saveState`, `loadState`, `printFullDiagnostics`
Depends on: All other .h files
Issues: 1880-line header with all implementation inline; blocking delay() in behavior execution; `calculateBehaviorOutcome` called 3x per medium cycle; face tracking duplicates ReflexiveControl logic; MAX_PEOPLE=10 not persisted

### BehaviorSelection.h
Purpose: Scores 8 candidate behaviors using needs/emotion/personality, applies repetition penalties, selects winner with hysteresis.
Key API: `scoreAllBehaviors`, `selectBehavior`, `forceAlternativeBehavior`, `isStuck`, `recordBehaviorExecution`
Depends on: Needs.h, Personality.h, Emotion.h, SpatialMemory.h, EpisodicMemory.h
Issues: `scoreAllBehaviorsWithMemory()` is dead code; `behaviorToString()` duplicated across files

### ServoController.h
Purpose: Wraps 3 servos with easing, emotion-driven jitter, direct-write bypass, and micro-movement helpers.
Key API: `initialize`, `smoothMoveTo`, `snapTo`, `directWrite`, `breathingMotion`, `weightShift`, `microTilt`, getters
Depends on: Servo.h, MovementStyle.h
Issues: `smoothMoveTo` is BLOCKING (for-loop with delay()); `weightShift`/`microTilt` also block; servo clamp range doesn't match per-servo limits elsewhere

### AttentionSystem.h
Purpose: 8-direction salience scoring from novelty/variance/change; shifts focus when salience crosses threshold.
Key API: `update`, `getFocusDirection`, `getMaxSalience`, `forceAttention`, `needsPeripheralSweep`, `needsFovealScan`
Depends on: SpatialMemory.h, Personality.h
Issues: `countHighSalienceDirections` buffer overflow risk if limit raised without updating caller

### ConsciousnessLayer.h
Purpose: Models inner life — epistemic states, motivational conflict, counterfactual thinking, wondering, meta-awareness, self-narrative.
Key API: `update`, `triggerCounterfactual`, `onEnvironmentChange`, `isWondering`, `isInConflict`, `getDeliberationDelay`, `shouldShowFalseStart`
Depends on: BehaviorSelection.h, Emotion.h, Personality.h, Needs.h, SpatialMemory.h, Learning.h
Issues: `environmentalStimulation` field is written but never read (dead field); `recentMoodTrend` calculation inverted

### ConsciousnessManifest.h
Purpose: Translates consciousness states into visible servo movements and buzzer sounds.
Key API: `manifestWondering`, `manifestConflict`, `manifestMetaCatch`, `manifestCounterfactual`, `manifestEpistemicState`
Depends on: ConsciousnessLayer.h, ServoController.h, BodySchema.h, Emotion.h, Personality.h
Issues: Extensive blocking delay() calls; unused parameters in `manifestConflict`; `WONDER_EXTERNAL` case unhandled

### GoalFormation.h
Purpose: Multi-step goal formation, pursuit, progress tracking, interruption, and abandonment.
Key API: `shouldFormGoal`, `formGoal`, `pursueSuggestedBehavior`, `recordProgress`, `completeGoal`, `abandonGoal`
Depends on: BehaviorSelection.h, Emotion.h, Personality.h, EpisodicMemory (forward-declared)
Issues: `formGoal` accepts unused Emotion& parameter

### IllusionLayer.h
Purpose: Creates visible behavioral signatures — deliberation pauses, micro-expressions, false starts, attentional dwelling, vocalizations.
Key API: `deliberate`, `microExpression`, `showIntentionConflict`, `attentionalDwell`, `vocalizeInternalState`, `showSelfCorrection`
Depends on: Emotion.h, BehaviorSelection.h, ServoController.h, MovementStyle.h
Issues: Blocking delay() (80-700ms); `behaviorToString`/`emotionToString` duplicated from other files

### Learning.h
Purpose: Multi-timescale learning — fast session weights, medium consolidation, slow personality drift, EEPROM persistence.
Key API: `recordOutcome`, `consolidate`, `getPersonalityEvidence`, `saveToEEPROM`, `loadFromEEPROM`
Depends on: EEPROM.h, Personality.h, BehaviorSelection.h
Issues: `fastWeights`/`mediumWeights` sized at 16 but only 8 behaviors exist; `totalUptime` overwritten not accumulated; weak additive checksum

### EpisodicMemory.h
Purpose: Circular buffer of 20 episodes with similarity recall, salience-weighted forgetting, consolidation.
Key API: `recordEpisode`, `recallSimilar`, `recallBestExperience`, `recallWorstExperience`, `hasExperienceWith`, `consolidate`
Depends on: Emotion.h, BehaviorSelection.h
Issues: `print()` "top 5 salient" loop broken — shows same episode 5x; `behaviorToString`/`emotionToString` duplicated

### SpatialMemory.h
Purpose: 8-directional spatial awareness — novelty, variance, change tracking, face presence, external vision injection.
Key API: `updateReading`, `injectExternalNovelty`, `recordFaceAt`, `getMostInterestingDirection`, `likelyHumanPresent`
Depends on: Personality.h
Issues: none found

### OutcomeCalculator.h
Purpose: Computes 0-1 outcome score after behavior execution by comparing before/after needs/emotion/goal snapshots.
Key API: `snapshotState`, `calculate`, `printBreakdown`
Depends on: Needs.h, Emotion.h, GoalFormation.h, BehaviorSelection.h
Issues: Max outcome capped at 0.80 when no goal is active (goal weight not redistributed)

### SpeechUrge.h
Purpose: Accumulates internal "pressure to speak" from triggers (loneliness, boredom, face events, startlement) with per-trigger cooldowns.
Key API: `update`, `proposeTrigger`, `utteranceCompleted`, `wantsToSpeak`, `getUrge`, `getTrigger`
Depends on: Needs.h, Emotion.h, Personality.h
Issues: `TRIGGER_GREETING` and `TRIGGER_COMMENTARY` defined but never triggered (dead code); `lastTriggerTime` array size hardcoded as magic number 13

### AnimationController.h
Purpose: Orchestrates behavior-driven pose sequences and procedural animations (nods, shakes, bounces, retreats).
Key API: `executeBehavior`, `transitionToPose`, `nodYes`, `shakeNo`, `playfulBounce`, `retreatMotion`, `expressEmotion`, `updateMicroMovements`
Depends on: ServoController.h, PoseLibrary.h, BehaviorSelection.h, Emotion.h, Personality.h
Issues: Line 324 writes `tiltServo` directly, bypassing ServoController; blocking delay() throughout

### MovementExpression.h
Purpose: Generates varied emotional gesture sequences with recent-expression tracking to avoid repetition.
Key API: `expressAgreement`, `expressCuriosity`, `expressExcitement`, `expressEmotion`, `performQuirk`, `curiousInspection`, `socialGreeting`
Depends on: Emotion.h, Personality.h, Needs.h, ServoController.h, MovementStyle.h
Issues: `recentExpressions` initialized to all AGREEMENT — first calls to other types may be incorrectly skipped; non-blocking refactoring incomplete (many delay() calls remain)

### MovementStyle.h
Purpose: Generates movement-quality parameters (speed, amplitude, smoothness) from emotion/personality/needs.
Key API: `generate`, `applyToPosition`, `getExcitedStyle`, `getAnxiousStyle`, `getContentStyle`
Depends on: Emotion.h, Personality.h, Needs.h
Issues: none found

### BodySchema.h
Purpose: Forward/inverse kinematics for servo-to-world coordinate conversion and attention-target gaze system.
Key API: `forwardKinematics`, `inverseKinematics`, `lookAt`, `setAttentionTarget`, `trackAttention`, `generateScanPattern`
Depends on: Arduino.h
Issues: IK always resets tilt to zero; `generateScanPattern` ignores distance params; `exploreRandomly` generates unreachable 360-degree targets

### PoseLibrary.h
Purpose: Behavior-specific base poses dynamically modulated by emotion and personality.
Key API: `generatePose`, `generateSequence`, `getNeutralPose`, `getStartupPose`, `interpolate`
Depends on: BehaviorSelection.h, Emotion.h, Personality.h
Issues: `Pose` and `ServoAngles` duplicate the same concept with no conversion

### ReflexiveControl.h
Purpose: Adaptive PID face-tracking controller with LOST/ACQUIRE/TRACK state machine, confidence modulation, oscillation detection.
Key API: `updateFaceData`, `updateConfidence`, `faceLost`, `calculate`, `getSearchPosition`, `reset`, `enable`, `disable`
Depends on: Arduino.h
Issues: static locals prevent multiple instances; `calculate` returns true even in LOST state; Ki never adapted by `updateGains`

### ScanningSystem.h
Purpose: 3-tier environmental scanning — ambient monitoring, peripheral sweep, foveal spiral scan.
Key API: `ambientMonitoring`, `peripheralSweepOptimized`, `fovealScan`, `orientToDirection`
Depends on: Servo.h, SpatialMemory.h, ServoController.h, MovementStyle.h
Issues: Legacy methods bypass ServoController (position desync risk); 3 of 8 directions never returned by `angleToDirection`

### AmbientLife.h
Purpose: Need-driven idle micro-movements — breathing, weight shifts, curious glances.
Key API: `update`
Depends on: Needs.h, Emotion.h, Personality.h, ServoController.h
Issues: Breathing offset may compound (nod drift); glance tilt never restored

### droidSpeak.h
Purpose: R2-D2 style buzzer sound effects — startup, happy, sad, alert, wondering, etc.
Key API: `droidSpeak` (legacy), `DroidSpeak::startup`, `happy`, `sad`, `alert`, `wondering`, `conflicted`, `chirp`
Depends on: LittleBots_Board_Pins.h
Issues: All sounds use blocking delay(); legacy function plays one extra beep (off-by-one)

### checkUltrasonic.h
Purpose: HC-SR04 ultrasonic distance measurement.
Key API: `checkUltra`
Depends on: Arduino core
Issues: No include guard; 400cm return indistinguishable from timeout; blocking pulseIn (30ms)

### LittleBots_Board_Pins.h
Purpose: Hardware pin definitions (echo=14, trig=15, buzzer=10).
Key API: `echoPin`, `trigPin`, `buzzerPin` (macros)
Depends on: nothing
Issues: No include guard; uses #define instead of constexpr

---

## ESP32 Firmware

### Buddy_ESP32_Bridge.ino
Purpose: ESP32-S3 WiFi bridge — MJPEG camera stream over HTTP, WebSocket/UDP command relay between PC and Teensy UART at 921600 baud.
Key API: `captureTask`, `httpServerTask`, `wsEvent`, `handleUDP`, `handleStream`, `handleCapture`, `handleHealth`, `checkTeensyUnsolicited`
Depends on: esp_camera.h, WiFi.h, WebServer.h, WiFiUdp.h, WebSocketsServer.h
Issues: Hardcoded placeholder WiFi credentials; `framesSent` incremented outside mutex; UART reads in `checkTeensyUnsolicited` lack mutex protection; network services not restarted after WiFi reconnect

### Buddy_esp32_cam_V18_debug.ino (legacy)
Purpose: Older ESP32-CAM firmware with on-device AI face detection + histogram tracking fallback, UART output to Teensy at 50Hz. Predecessor to the bridge architecture.
Key API: `calculateConfidence`, `captureAndRotate`, `captureWithRetry`, `reinitializeCamera`, `handleCapture`
Depends on: eloquent_esp32cam, esp_camera.h, HistogramTracker.h, WiFi.h, WebServer.h
Issues: Hardcoded placeholder WiFi credentials; `jpegCaptureInProgress` flag not thread-safe; single-core bottleneck; WiFi reconnect blocks face tracking for 5s; infinite hang on camera failure

### HistogramTracker.h
Purpose: Histogram-based face tracker bridging AI detection gaps using hue/saturation histograms with skin-tone validation and two-stage spatial search.
Key API: `buildSignature`, `track`, `isSignatureValid`, `getTrackingQuality`, `invalidate`
Depends on: esp_camera.h, Arduino.h
Issues: Hardcoded 240x240 frame size; integer division in HSV conversion; expensive fine search (169 evaluations per frame)

---

## Command Protocol

Every `!COMMAND` that AIBridge.h handles:

| Command | What it does |
|---|---|
| `!QUERY` | Returns full state JSON |
| `!LOOK:base,nod` | Move servos to position |
| `!SATISFY:need,amt` | Satisfy a homeostatic need |
| `!PRESENCE` | Simulate human presence detected |
| `!EXPRESS:emotion` | Play emotion animation |
| `!NOD:count` | Nod yes animation |
| `!SHAKE:count` | Shake no animation |
| `!STREAM:on/off` | Toggle periodic state broadcast |
| `!ATTENTION:dir` | Look in named direction |
| `!LISTENING` | Move to attentive pose |
| `!THINKING` | Start pondering loop animation |
| `!STOP_THINKING` | Stop thinking animation |
| `!SPEAKING` | Start speaking loop animation |
| `!STOP_SPEAKING` | Stop speaking animation |
| `!ACKNOWLEDGE` | Quick subtle nod gesture |
| `!CELEBRATE` | Play happy bounce animation |
| `!IDLE` | Clear AI state, go neutral |
| `!SPOKE` | Acknowledge speech happened |
| `!VISION:json` | Update from PC vision data |
| `!VISION json` | Rich scene context update |
| `!PERFORM:type` | Speech performance arc move |
| `!PHYSICAL:name` | Physical expression gesture |

---

## Data Flow Summary

1. **Face tracking:** ESP32-CAM MJPEG stream > `buddy_vision.py` MediaPipe detection > UDP `FACE:x,y,vx,vy,w,h,conf,seq` > ESP32 Bridge UART > Teensy `ReflexiveControl` PID > servos
2. **Human speech:** Mic > Porcupine wake word OR attention-triggered VAD > record PCM > Whisper STT > Ollama LLM (with narrative context) > edge-tts MP3 > browser `<audio>` playback + `!SPEAKING`/`!STOP_SPEAKING` to Teensy
3. **Spontaneous speech:** `SpeechUrge` (Teensy) + `check_spontaneous_speech` (Python) > intent selection > strategy > `build_narrative_prompt` > Ollama > edge-tts > browser audio + physical performance arc
4. **Scene understanding:** ESP32 camera `/capture` > Ollama LLaVA description > `SceneContext` objects/changes/novelty > `SalienceFilter` scoring > `!VISION:json` to Teensy > emotion nudges + scanning behavior
5. **State sync:** Teensy `!QUERY` response (JSON: emotion, needs, behavior, servos) > `teensy_state` dict > SocketIO `buddy_state` emit > browser dashboard bars + narrative engine mood update

---

## Top Issues Found

1. **[CRITICAL] buddy_web_full_V2.py:** Lock leak paths — 11 locks with suspected leaks on exception paths. Watchdog force-releases `processing_lock` after 90s as mitigation, but other locks have no such safety net.
2. **[CRITICAL] buddy_web_full_V2.py:** Stuck "Transcribing..." state requires server restart. Whisper/Ollama threads that survive their `join(timeout)` continue running indefinitely.
3. **[CRITICAL] buddy_web_full_V2.py:** Wake word crash — porcupine access violation race condition between `wake_word_loop` and `pause_wake_word`/`resume_wake_word`.
4. **[CRITICAL] Buddy_ESP32_Bridge.ino:** UART reads in `checkTeensyUnsolicited()` lack mutex protection — concurrent reads from `wsEvent`/`handleUDP` can corrupt data.
5. **[IMPORTANT] Emotion.h / AIBridge.h:** `nudge()` parameter order swapped between 2-arg `(valence, arousal)` and 3-arg `(arousal, valence, dominance)` — silent misuse likely.
6. **[IMPORTANT] ServoController.h:** `smoothMoveTo` is blocking (delay() in loop) — contradicts non-blocking design intent and blocks 50Hz main loop.
7. **[IMPORTANT] buddy_web_full_V2.py:** 4500-line monolith with 830-line inline HTML template — difficult to maintain and test.
8. **[IMPORTANT] BehaviorEngine.h:** 1880-line header file with all implementation inline — slow compilation, hard to maintain.
9. **[IMPORTANT] Multiple firmware files:** Pervasive blocking `delay()` calls in animation/expression/sound code (ConsciousnessManifest, IllusionLayer, AnimationController, MovementExpression, droidSpeak) — blocks main loop for 80-700ms per call.
10. **[IMPORTANT] EpisodicMemory.h:** `print()` "top 5 salient" loop broken — shows same episode 5 times instead of top 5.
11. **[IMPORTANT] Buddy_ESP32_Bridge.ino / Buddy_esp32_cam.ino:** Hardcoded placeholder WiFi credentials — will fail to connect out of the box.
12. **[MINOR] checkUltrasonic.h / LittleBots_Board_Pins.h:** Missing include guards — multiple inclusion causes redefinition warnings or linker errors.
13. **[MINOR] Multiple files:** `behaviorToString()` / `emotionToString()` duplicated in BehaviorSelection, BehaviorEngine, IllusionLayer, EpisodicMemory — can diverge.
14. **[MINOR] Learning.h:** `totalUptime` overwritten on save instead of accumulated — lifetime uptime tracking broken.
15. **[MINOR] OutcomeCalculator.h:** Max outcome capped at 0.80 when no goal active — goal weight (0.20) not redistributed.

---

## Missing or Stubbed

1. **`SpeechUrge.TRIGGER_GREETING` / `TRIGGER_COMMENTARY`** — defined in enum, handled in `triggerToString()`, but never triggered in `update()`.
2. **`BehaviorSelection.scoreAllBehaviorsWithMemory()`** — implemented but never called (dead code).
3. **`BodySchema.inverseKinematics` tilt output** — always returns tiltZero with "could add expressiveness later" comment.
4. **`ConsciousnessLayer.environmentalStimulation`** — field written by `onEnvironmentChange()` but never read.
5. **`MovementStyle` preset methods** (`getExcitedStyle`, `getAnxiousStyle`, `getContentStyle`) — defined but never called.
6. **Object detection in buddy_vision.py** — `state.objects` list exists but no YOLO or object detector is integrated; always empty.
7. **Person identification** — `narrative_engine.person_profiles` supports multi-person tracking but no face recognition is implemented; relies on single-person assumption.
8. **ESP32 Bridge network service restart** — `startNetworkServices()` is guarded to run once; after WiFi reconnect, HTTP/WS/UDP servers are not restarted.
9. **Teensy EEPROM person records** — `BehaviorEngine.MAX_PEOPLE=10` in-memory only; lost on reset.
10. **`ScanningSystem.angleToDirection`** — only maps 5 of 8 compass directions; back-right and back-left never returned.
