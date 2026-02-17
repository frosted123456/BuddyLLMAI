"""
attention_detector.py — Buddy's Attention Detection System
============================================================

Detects when a person is paying attention to Buddy (looking directly at him)
and triggers listening mode without requiring a wake word.

Architecture:
  - AttentionDetector: accumulates "facing camera" signals over a rolling window
  - Handles Buddy's own servo movement (freezes state during movement)
  - Provides attention state: ABSENT → PRESENT → ATTENTIVE
  - VoiceActivityDetector: wraps Silero VAD or falls back to amplitude-based

States:
  ABSENT:    No face detected
  PRESENT:   Face detected but not facing Buddy
  ATTENTIVE: Person facing Buddy for 1.5s cumulative within 3s rolling window

Integration:
  - buddy_vision.py provides facing_camera via head pose estimation (solvePnP)
  - buddy_web_full_V2.py polls vision state and feeds it here
  - When ATTENTIVE + VAD detects speech → start recording (same as wake word path)
  - Ready signal: subtle servo movement when attention first detected

Threading:
  - Uses its own lock. Never holds it across blocking I/O.
  - Called from check_spontaneous_speech (~1s polls) and wake_word_loop (~30ms).
  - VoiceActivityDetector._process_silero uses a dedicated _model_lock
    (Silero RNN model is NOT thread-safe).
"""

import time
import threading
from collections import deque


# ═══════════════════════════════════════════════════════
# ATTENTION DETECTOR
# ═══════════════════════════════════════════════════════

class AttentionDetector:
    """
    Accumulates "facing Buddy" signals over a rolling window.
    Outputs attention state: ABSENT, PRESENT, ATTENTIVE.

    Design constraints:
      - 1.5s cumulative facing within 3s rolling window → ATTENTIVE
      - Freezes during Buddy's servo movement (camera swings cause detection drops)
      - Debounces transitions to avoid flicker
    """

    # Attention states
    ABSENT = "absent"
    PRESENT = "present"
    ATTENTIVE = "attentive"

    # Configuration
    WINDOW_SECONDS = 3.0        # Rolling window size
    THRESHOLD_SECONDS = 1.5     # Cumulative facing time needed within window
    DEBOUNCE_DOWN_MS = 500      # ms before downgrading from ATTENTIVE
    COOLDOWN_AFTER_LISTEN = 5.0 # Don't re-trigger for 5s after recording
    FREEZE_TIMEOUT = 5.0        # Safety: auto-unfreeze after 5s (prevents stuck freeze)

    def __init__(self):
        self.lock = threading.Lock()

        # Rolling window of (timestamp, facing_camera_bool) samples
        self._samples = deque(maxlen=300)  # ~3s at 100Hz, plenty

        # Current state
        self._state = self.ABSENT
        self._state_since = 0                # when state last changed
        self._attentive_since = 0            # when ATTENTIVE was reached
        self._last_attentive_confirmed = 0   # last time ATTENTIVE was valid (for debounce)

        # Freeze during Buddy movement
        self._frozen = False
        self._frozen_at = 0
        self._last_known_facing = False  # preserved during freeze

        # Cooldown after recording was triggered
        self._listen_cooldown_until = 0

        # Callbacks (set by main server BEFORE threads start)
        self.on_attentive = None   # Called once when entering ATTENTIVE
        self.on_lost = None        # Called once when leaving ATTENTIVE

        # Stats
        self._total_attentive_events = 0

    def update(self, face_detected, facing_camera):
        """
        Feed a new vision sample. Call this every time vision state is polled.
        face_detected: bool — is any face visible?
        facing_camera: bool — is the primary face oriented toward Buddy?
        """
        now = time.time()

        with self.lock:
            # Safety: auto-unfreeze after timeout (prevents stuck freeze if
            # unfreeze() was never called due to a crash in servo movement code)
            if self._frozen and (now - self._frozen_at) > self.FREEZE_TIMEOUT:
                self._frozen = False

            if self._frozen:
                # During Buddy movement, keep last known state
                # but still record the timestamp so the window advances
                self._samples.append((now, self._last_known_facing))
            else:
                self._samples.append((now, facing_camera and face_detected))
                self._last_known_facing = facing_camera and face_detected

            # Prune samples outside the rolling window
            cutoff = now - self.WINDOW_SECONDS
            while self._samples and self._samples[0][0] < cutoff:
                self._samples.popleft()

            # Calculate cumulative facing time within window
            facing_time = self._calculate_facing_time()

            # Determine new state
            old_state = self._state

            if not face_detected and not self._frozen:
                new_state = self.ABSENT
            elif facing_time >= self.THRESHOLD_SECONDS:
                new_state = self.ATTENTIVE
            elif face_detected or self._frozen:
                new_state = self.PRESENT
            else:
                new_state = self.ABSENT

            # Track last time ATTENTIVE was confirmed (for debounce)
            if new_state == self.ATTENTIVE:
                self._last_attentive_confirmed = now

            # Debounce: don't drop from ATTENTIVE too quickly
            # Uses _last_attentive_confirmed (last time ATTENTIVE was valid),
            # NOT _attentive_since (when ATTENTIVE was first entered).
            if old_state == self.ATTENTIVE and new_state != self.ATTENTIVE:
                ms_since_confirmed = (now - self._last_attentive_confirmed) * 1000
                if ms_since_confirmed < self.DEBOUNCE_DOWN_MS:
                    new_state = self.ATTENTIVE  # Hold ATTENTIVE a bit longer

            # Apply state transition
            if new_state != old_state:
                self._state = new_state
                self._state_since = now

                if new_state == self.ATTENTIVE:
                    self._attentive_since = now
                    self._total_attentive_events += 1
                    callback = self.on_attentive
                elif old_state == self.ATTENTIVE:
                    callback = self.on_lost
                else:
                    callback = None
            else:
                callback = None

        # Fire callbacks OUTSIDE lock to prevent deadlocks
        if callback:
            try:
                callback()
            except Exception as e:
                print(f"[ATTENTION] Callback error: {e}")

    def _calculate_facing_time(self):
        """
        Calculate cumulative facing time from samples in the rolling window.
        Must be called with self.lock held.
        Credits time to an interval only when BOTH endpoints have facing=True
        (prevents inflating facing_time from a single True sample).
        """
        if len(self._samples) < 2:
            return 0.0

        total = 0.0
        prev_ts, prev_facing = self._samples[0]
        for i in range(1, len(self._samples)):
            ts, facing = self._samples[i]
            # Only credit time when BOTH the previous and current sample are facing
            if prev_facing and facing:
                dt = ts - prev_ts
                # Cap individual intervals to 1.2s to handle gaps.
                # Note: update() is called from teensy_poll_loop at ~1Hz, so
                # normal intervals are ~1s. Old 0.5s cap halved every interval,
                # making it impossible to reach 1.5s threshold in 3s window.
                total += min(dt, 1.2)
            prev_ts, prev_facing = ts, facing

        return total

    def freeze(self):
        """
        Freeze attention state during Buddy's servo movement.
        The last known facing state is preserved until unfreeze().
        """
        with self.lock:
            self._frozen = True
            self._frozen_at = time.time()

    def unfreeze(self):
        """Resume normal attention detection after servo movement stops."""
        with self.lock:
            self._frozen = False

    def record_listen_triggered(self):
        """Mark that listening was triggered — starts cooldown."""
        with self.lock:
            self._listen_cooldown_until = time.time() + self.COOLDOWN_AFTER_LISTEN

    def get_state(self):
        """Return current attention state string."""
        with self.lock:
            return self._state

    def is_attentive(self):
        """Return True if person is paying attention to Buddy."""
        with self.lock:
            return self._state == self.ATTENTIVE

    def can_trigger_listen(self):
        """Check if attention-triggered listening is allowed (not in cooldown)."""
        with self.lock:
            if self._state != self.ATTENTIVE:
                return False
            if time.time() < self._listen_cooldown_until:
                return False
            return True

    def get_status(self):
        """Return full status dict for debug/API."""
        with self.lock:
            return {
                "state": self._state,
                "state_since": self._state_since,
                "frozen": self._frozen,
                "facing_time": round(self._calculate_facing_time(), 2),
                "sample_count": len(self._samples),
                "total_attentive_events": self._total_attentive_events,
                "in_cooldown": time.time() < self._listen_cooldown_until,
            }


# ═══════════════════════════════════════════════════════
# VOICE ACTIVITY DETECTOR — Silero VAD with amplitude fallback
# ═══════════════════════════════════════════════════════

class VoiceActivityDetector:
    """
    Detects speech in audio frames. Uses Silero VAD if torch is available,
    falls back to simple amplitude-based detection.

    Expected input: list/array of int16 PCM samples at 16kHz.

    Threading: _model_lock protects the Silero model (RNN with internal state,
    NOT thread-safe). Called only from wake_word_loop in practice, but the lock
    ensures safety if call sites change in the future.
    """

    def __init__(self):
        self.lock = threading.Lock()
        self._model_lock = threading.Lock()  # Protects Silero model inference
        self._vad_model = None
        self._vad_available = False
        self._init_attempted = False
        self._init_complete = False  # True only after init fully finishes

        # Amplitude fallback state
        self._consecutive_speech_frames = 0
        self._consecutive_silence_frames = 0
        self._speech_active = False

        # Configuration
        self.AMPLITUDE_THRESHOLD = 600    # Minimum amplitude to count as speech
        self.SPEECH_FRAMES_NEEDED = 4     # Consecutive speech frames to trigger
        self.SILENCE_FRAMES_TO_STOP = 8   # Consecutive silence frames to stop

    def _lazy_init(self):
        """Try to load Silero VAD on first use. Thread-safe."""
        with self.lock:
            if self._init_attempted:
                return
            self._init_attempted = True

        # Load outside lock — may download model (blocking I/O)
        try:
            import torch
            model, utils = torch.hub.load(
                repo_or_dir='snakers4/silero-vad',
                model='silero_vad',
                trust_repo=True,
                verbose=False
            )
            # Reset initial state to ensure clean start
            model.reset_states()
            with self.lock:
                self._vad_model = model
                self._vad_available = True
                self._init_complete = True
            print("[VAD] Silero VAD loaded successfully")
        except Exception as e:
            print(f"[VAD] Silero VAD not available ({e}), using amplitude fallback")
            with self.lock:
                self._vad_available = False
                self._init_complete = True

    def is_ready(self):
        """Return True if initialization is complete (Silero loaded or fallback active)."""
        with self.lock:
            return self._init_complete

    def process_frame(self, pcm_samples, sample_rate=16000):
        """
        Process a single audio frame (typically 512 samples at 16kHz).
        Returns: float probability of speech (0.0-1.0).

        For Silero VAD: returns model confidence.
        For amplitude fallback: returns 0.0 or 1.0.

        Will NOT block for model download — falls back to amplitude if init
        is still in progress.
        """
        with self.lock:
            use_silero = self._vad_available
            model = self._vad_model

        if use_silero and model is not None:
            return self._process_silero(pcm_samples, model, sample_rate)
        else:
            return self._process_amplitude(pcm_samples)

    def _process_silero(self, pcm_samples, model, sample_rate):
        """Process frame through Silero VAD. Holds _model_lock during inference."""
        try:
            import torch
            # Convert int16 PCM to float32 tensor normalized to [-1, 1]
            if isinstance(pcm_samples, list):
                audio = torch.FloatTensor(pcm_samples) / 32768.0
            else:
                audio = torch.FloatTensor(list(pcm_samples)) / 32768.0

            # Silero VAD expects specific frame sizes: 256, 512, or 768 at 16kHz
            # PvRecorder provides 512 which is compatible
            with self._model_lock:
                confidence = model(audio, sample_rate).item()
            return confidence
        except Exception:
            # Fall back to amplitude on any error
            return self._process_amplitude(pcm_samples)

    def _process_amplitude(self, pcm_samples):
        """Simple amplitude-based voice detection fallback."""
        if pcm_samples is None or len(pcm_samples) == 0:
            return 0.0

        amp = max(abs(min(pcm_samples)), abs(max(pcm_samples)))

        with self.lock:
            if amp > self.AMPLITUDE_THRESHOLD:
                self._consecutive_speech_frames += 1
                self._consecutive_silence_frames = 0
            else:
                self._consecutive_silence_frames += 1
                if self._consecutive_silence_frames >= self.SILENCE_FRAMES_TO_STOP:
                    self._consecutive_speech_frames = 0
                    self._speech_active = False

            if self._consecutive_speech_frames >= self.SPEECH_FRAMES_NEEDED:
                self._speech_active = True
                return 0.9  # High confidence
            elif self._speech_active:
                return 0.7  # Sustained speech
            else:
                return 0.0

    def is_speech(self, pcm_samples, threshold=0.5, sample_rate=16000):
        """Convenience: returns True if speech probability exceeds threshold."""
        return self.process_frame(pcm_samples, sample_rate) > threshold

    def reset(self):
        """Reset internal state (call after recording ends)."""
        with self.lock:
            self._consecutive_speech_frames = 0
            self._consecutive_silence_frames = 0
            self._speech_active = False
        # Reset Silero model state under _model_lock (NOT self.lock — no nesting)
        with self._model_lock:
            # Read model ref under self.lock, then use under _model_lock
            with self.lock:
                model = self._vad_model
            if model is not None:
                try:
                    model.reset_states()
                except Exception:
                    pass
