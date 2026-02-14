"""
audio_context.py — Buddy's Ambient Room Listening System
=========================================================

Background audio monitoring that passively listens to the room,
detects speech via VAD, transcribes when speech is detected, and
feeds overheard context into Buddy's narrative engine.

Uses webrtcvad for lightweight Voice Activity Detection.
Shares the PvRecorder via a frame queue fed by wake_word_loop().

Privacy note: This feature is OFF by default. Must be explicitly
enabled via the settings panel.

Requirements:
    pip install webrtcvad
"""

import time
import threading
import struct
import tempfile
import wave
import os
import collections

# webrtcvad expects 16-bit PCM at 16kHz in 10/20/30ms frames
try:
    import webrtcvad
    HAS_WEBRTCVAD = True
except ImportError:
    HAS_WEBRTCVAD = False
    print("[AUDIO_CTX] webrtcvad not installed — ambient listening unavailable")


class AudioContext:
    """
    Background ambient audio monitoring for Buddy.
    Detects speech in the room, transcribes it, and feeds context
    into the narrative engine.

    Uses Voice Activity Detection (VAD) to avoid running Whisper on silence.
    Only transcribes when:
    - VAD detects speech
    - Buddy is NOT speaking (echo suppression)
    - Ambient mode is enabled in config
    """

    def __init__(self):
        self.enabled = False
        self.running = False
        self.thread = None

        # Transcript history
        self.transcript_history = collections.deque(maxlen=20)
        self.current_room_context = ""
        self.last_transcription_time = 0
        self.transcription_interval = 8   # Min seconds between transcriptions

        # VAD
        self.vad = None
        self._vad_mode = 2  # 0=least aggressive, 3=most aggressive

        # Frame queue — fed by wake_word_loop()
        self.frame_queue = collections.deque(maxlen=200)  # ~6.4 seconds at 32ms/frame

        # Speech accumulation buffer
        self._speech_frames = []
        self._speech_started = False
        self._speech_start_time = 0
        self._silence_after_speech = 0
        self._vad_silence_threshold = 15  # ~480ms of silence = speech ended

        # External references (set by main server)
        self.whisper_model = None
        self.narrative_engine = None
        self.intent_manager = None
        self.socketio = None

        # Echo suppression — timestamp until which we skip processing
        # Set by main server when TTS plays
        self.tts_playing_until = 0

        # Salience thresholds
        self.salience_store_threshold = 3
        self.salience_react_threshold = 5

        # Thread safety
        self.lock = threading.Lock()

    def start(self):
        """Start ambient listening on a background thread."""
        if not HAS_WEBRTCVAD:
            if self.socketio:
                self.socketio.emit('log', {
                    'message': 'Cannot start ambient listening: webrtcvad not installed',
                    'level': 'error'
                })
            return False

        if self.running:
            return True

        try:
            self.vad = webrtcvad.Vad(self._vad_mode)
        except Exception as e:
            print(f"[AUDIO_CTX] VAD init failed: {e}")
            return False

        self.running = True
        self.enabled = True
        self.thread = threading.Thread(
            target=self._listen_loop,
            daemon=True,
            name="audio-context"
        )
        self.thread.start()
        print("[AUDIO_CTX] Ambient listening started")
        if self.socketio:
            self.socketio.emit('log', {
                'message': 'Ambient listening started',
                'level': 'success'
            })
        return True

    def stop(self):
        """Stop ambient listening."""
        self.running = False
        self.enabled = False
        self._speech_frames = []
        self._speech_started = False
        print("[AUDIO_CTX] Ambient listening stopped")

    def feed_frame(self, pcm_frame):
        """
        Called by wake_word_loop() to feed audio frames.
        pcm_frame: list of 16-bit signed integers (from PvRecorder).
        """
        if self.enabled and self.running:
            self.frame_queue.append(pcm_frame)

    def _listen_loop(self):
        """
        Background thread:
        1. Read audio frames from the queue
        2. Run VAD on each frame
        3. When speech detected, accumulate frames
        4. When speech ends (silence), transcribe the buffer
        5. Score transcription for salience
        6. If salient, feed into narrative engine
        """
        # webrtcvad needs 10/20/30ms frames at 16kHz
        # PvRecorder gives us 512-sample frames = 32ms at 16kHz
        # We'll use 480-sample (30ms) sub-frames for VAD
        VAD_FRAME_SIZE = 480  # 30ms at 16kHz
        leftover = []

        while self.running:
            try:
                # Get frame from queue (non-blocking with short sleep)
                if not self.frame_queue:
                    time.sleep(0.01)
                    continue

                try:
                    pcm = self.frame_queue.popleft()
                except IndexError:
                    time.sleep(0.01)
                    continue

                # Echo suppression: skip processing while TTS is playing
                now = time.time()
                if now < self.tts_playing_until:
                    # Drain but don't process — prevent false triggers
                    self._speech_frames = []
                    self._speech_started = False
                    continue

                # Convert PvRecorder frame (list of ints) to bytes for VAD
                # Prepend any leftover samples from the previous frame
                all_samples = leftover + list(pcm)
                leftover = []

                # Process in VAD_FRAME_SIZE chunks
                idx = 0
                while idx + VAD_FRAME_SIZE <= len(all_samples):
                    chunk = all_samples[idx:idx + VAD_FRAME_SIZE]
                    chunk_bytes = struct.pack(f"{VAD_FRAME_SIZE}h", *chunk)

                    try:
                        is_speech = self.vad.is_speech(chunk_bytes, 16000)
                    except Exception:
                        is_speech = False

                    if is_speech:
                        if not self._speech_started:
                            self._speech_started = True
                            self._speech_start_time = now
                            self._silence_after_speech = 0
                        self._speech_frames.extend(chunk)
                        self._silence_after_speech = 0
                    else:
                        if self._speech_started:
                            self._silence_after_speech += 1
                            self._speech_frames.extend(chunk)

                            # Speech ended — enough silence after speech
                            if self._silence_after_speech >= self._vad_silence_threshold:
                                self._process_speech_buffer(now)

                    idx += VAD_FRAME_SIZE

                # Save leftover samples for next iteration
                if idx < len(all_samples):
                    leftover = all_samples[idx:]

                # Safety: if speech buffer gets too long (>30 seconds), force process
                max_samples = 16000 * 30  # 30 seconds
                if len(self._speech_frames) > max_samples:
                    self._process_speech_buffer(now)

            except Exception as e:
                print(f"[AUDIO_CTX] Listen loop error: {e}")
                time.sleep(1)

    def _process_speech_buffer(self, now):
        """Process accumulated speech frames: transcribe and score."""
        frames = self._speech_frames
        self._speech_frames = []
        self._speech_started = False
        self._silence_after_speech = 0

        # Rate limiting: don't transcribe too frequently
        if now - self.last_transcription_time < self.transcription_interval:
            return

        # Minimum speech length: ~0.5 seconds (8000 samples at 16kHz)
        if len(frames) < 8000:
            return

        # Don't transcribe if TTS just finished (extra buffer for room echo)
        if now < self.tts_playing_until + 3.0:
            return

        self.last_transcription_time = now

        # Transcribe in a separate step to avoid blocking the listen loop
        threading.Thread(
            target=self._transcribe_and_score,
            args=(frames, now),
            daemon=True,
            name="ambient-transcribe"
        ).start()

    def _transcribe_and_score(self, frames, timestamp):
        """Transcribe speech frames and score for salience."""
        if not self.whisper_model:
            return

        # Write frames to temp WAV file
        try:
            with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as f:
                wav_path = f.name
                with wave.open(f.name, "wb") as w:
                    w.setnchannels(1)
                    w.setsampwidth(2)
                    w.setframerate(16000)
                    w.writeframes(struct.pack(f"{len(frames)}h", *frames))

            # Transcribe with Whisper
            result = self.whisper_model.transcribe(wav_path, fp16=False)
            text = result["text"].strip()

        except Exception as e:
            print(f"[AUDIO_CTX] Transcription error: {e}")
            return
        finally:
            try:
                os.unlink(wav_path)
            except Exception:
                pass

        # Filter out garbage (very short or obviously noise)
        if not text or len(text) < 5:
            return

        # Filter out common Whisper hallucinations on noise
        hallucination_patterns = [
            "thank you", "thanks for watching", "subscribe",
            "like and subscribe", "see you next time",
            "you", "bye", "okay",
        ]
        text_lower = text.lower().strip()
        if text_lower in hallucination_patterns:
            return

        # Score for salience
        score = self.score_transcript(text)

        print(f"[AUDIO_CTX] Overheard: \"{text[:80]}\" (salience: {score})")

        if self.socketio:
            self.socketio.emit('log', {
                'message': f'Overheard: "{text[:60]}" (salience: {score})',
                'level': 'debug'
            })

        # Store if above threshold
        if score >= self.salience_store_threshold:
            with self.lock:
                self.transcript_history.append({
                    "text": text,
                    "time": timestamp,
                    "salience": score,
                })
                self._rebuild_room_context()

            # Feed into narrative engine
            if self.narrative_engine:
                self.narrative_engine.record_overheard(text, score)

            # If high salience, trigger react_to_overheard intent
            if score >= self.salience_react_threshold and self.intent_manager:
                self.intent_manager.set_intent(
                    "react_to_overheard",
                    reason=f"Overheard: {text[:50]}"
                )

    def score_transcript(self, text):
        """
        Score overheard speech for relevance to Buddy.
        Higher score = more likely Buddy should care about it.

        High (7-10): Buddy's name mentioned, direct questions, mentions of robot
        Medium (4-6): Interesting topics, emotional speech, mentions of objects Buddy can see
        Low (1-3): Generic conversation, phone calls, background chatter
        Suppress (0): TV/radio audio, music, repetitive noise
        """
        score = 1  # Base score for any detected speech
        text_lower = text.lower()

        # High relevance: Buddy's name or robot references
        buddy_names = ["buddy", "robot", "le robot", "petit robot", "p'tit robot"]
        if any(name in text_lower for name in buddy_names):
            score += 6

        # High: direct questions or addressing
        question_markers = ["?", "tu penses", "qu'est-ce que", "what do you",
                          "do you think", "hey", "look at"]
        if any(marker in text_lower for marker in question_markers):
            score += 3

        # Medium: emotional content
        emotion_words = ["wow", "oh", "ah", "incroyable", "amazing", "funny",
                        "weird", "bizarre", "strange", "cool", "awesome",
                        "terrible", "horrible", "beautiful", "drôle", "cute"]
        if any(word in text_lower for word in emotion_words):
            score += 2

        # Medium: objects Buddy might see on the desk
        desk_objects = ["mug", "cup", "coffee", "phone", "book", "laptop",
                       "keyboard", "mouse", "pen", "headphones", "monitor",
                       "tasse", "café", "livre", "écran"]
        if any(obj in text_lower for obj in desk_objects):
            score += 2

        # Medium: topics Buddy finds interesting
        interesting_topics = ["ai", "intelligence", "consciousness", "robot",
                            "philosophy", "space", "universe", "time",
                            "future", "technology", "science"]
        if any(topic in text_lower for topic in interesting_topics):
            score += 2

        # Low: very short or generic
        if len(text) < 10:
            score = max(1, score - 1)

        # Suppress: likely TV/radio/music (long monologue, no pauses)
        if len(text) > 500:
            score = max(0, score - 3)

        return min(10, score)

    def _rebuild_room_context(self):
        """Rebuild the room context summary from recent transcripts. Caller must hold self.lock."""
        now = time.time()
        recent = [t for t in self.transcript_history if now - t["time"] < 300]  # Last 5 minutes

        if not recent:
            self.current_room_context = ""
            return

        parts = ["What you've overheard recently:"]
        for t in recent[-5:]:  # Last 5 snippets
            age = int(now - t["time"])
            if age < 60:
                time_str = f"{age}s ago"
            else:
                time_str = f"{age // 60}min ago"
            text_short = t["text"][:80]
            if len(t["text"]) > 80:
                text_short += "..."
            parts.append(f'  - "{text_short}" ({time_str}, relevance: {t["salience"]})')

        self.current_room_context = "\n".join(parts)

    def get_room_context(self):
        """
        Return a string summarizing what Buddy has overheard recently.
        Formatted for inclusion in LLM prompt context.
        """
        with self.lock:
            return self.current_room_context

    def get_transcript_history(self):
        """Return a copy of the transcript history for UI display."""
        with self.lock:
            return list(self.transcript_history)
