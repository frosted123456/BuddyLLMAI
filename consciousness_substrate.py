"""
consciousness_substrate.py — Buddy's Cumulative Experience & Felt State
========================================================================

This module provides the "substrate" that makes Buddy feel conscious:
experiences that accumulate and CHANGE BEHAVIOR, not just prompt text.

Architecture:
  - ExperienceMemory: vector-based storage (short-term deque + long-term on disk)
  - SomaticState: body-level emotional accumulation (tension, warmth, restlessness)
  - EmotionalBaseline: slow-drifting personality parameters (trust, openness, resilience)
  - AnticipatoryModel: learns person patterns → generates expectations → surprise
  - BackgroundProcessor: consolidates short→long term, processes residue, updates predictions

Integration:
  - AFTER speech resolution: record_experience() stores the full situation
  - BEFORE intent selection: get_behavioral_bias() modifies escalation/strategy/timing
  - get_somatic_influence() drives physical expression independently of speech
  - get_felt_sense() provides emotional coloring for LLM (not facts, feelings)
  - Persistence: save/load alongside buddy_memory.json

No new locks needed — uses its own internal lock following existing patterns.
No modifications to existing methods — purely additive via new hook points.
"""

import time
import math
import json
import os
import threading
import logging
from collections import deque

logger = logging.getLogger(__name__)

# ═══════════════════════════════════════════════════════
# VECTOR OPERATIONS — Pure Python, no numpy needed
# ═══════════════════════════════════════════════════════

def _dot(a, b):
    """Dot product of two vectors."""
    return sum(x * y for x, y in zip(a, b))

def _norm(v):
    """L2 norm of a vector."""
    return math.sqrt(sum(x * x for x in v))

def _cosine_similarity(a, b):
    """Cosine similarity between two vectors. Returns 0.0 on degenerate input."""
    na, nb = _norm(a), _norm(b)
    if na < 1e-9 or nb < 1e-9:
        return 0.0
    return _dot(a, b) / (na * nb)


# ═══════════════════════════════════════════════════════
# VECTOR STORE — Simple JSON-backed persistent storage
# ═══════════════════════════════════════════════════════

class VectorStore:
    """
    Lightweight persistent vector store.
    Stores experience embeddings + metadata as JSON on disk.
    Uses cosine similarity for retrieval.
    Designed for hundreds of entries (days/weeks of robot life), not millions.
    """

    def __init__(self, filepath="buddy_experiences.json", max_entries=500):
        self.filepath = filepath
        self.max_entries = max_entries
        self._entries = []  # [{embedding, metadata, timestamp}, ...]
        self._load()

    def _load(self):
        """Load entries from disk."""
        try:
            with open(self.filepath, "r") as f:
                data = json.load(f)
            self._entries = data.get("entries", [])
            logger.info(
                "[CONSCIOUSNESS] Loaded %d long-term experiences from %s",
                len(self._entries), self.filepath
            )
        except (FileNotFoundError, json.JSONDecodeError):
            self._entries = []

    def save(self):
        """Persist entries to disk (atomic write)."""
        try:
            tmp = self.filepath + ".tmp"
            with open(tmp, "w") as f:
                json.dump({"entries": self._entries}, f)
            os.replace(tmp, self.filepath)
        except Exception as e:
            logger.error("[CONSCIOUSNESS] VectorStore save failed: %s", e)

    def add(self, embedding, metadata):
        """Add an experience. Evicts oldest if full."""
        self._entries.append({
            "embedding": embedding,
            "metadata": metadata,
            "timestamp": time.time(),
        })
        # Evict oldest if over capacity
        if len(self._entries) > self.max_entries:
            self._entries = self._entries[-self.max_entries:]

    def search(self, query_embedding, k=5, min_similarity=0.3):
        """
        Find k most similar experiences.
        Returns list of metadata dicts, sorted by similarity (descending).
        Only compares embeddings of the same dimension to avoid garbage results
        when switching between real embeddings and text-hash fallback.
        """
        if not self._entries or not query_embedding:
            return []

        query_dim = len(query_embedding)
        scored = []
        for entry in self._entries:
            emb = entry.get("embedding")
            if not emb or len(emb) != query_dim:
                continue  # Skip dimension-mismatched entries
            sim = _cosine_similarity(query_embedding, emb)
            if sim >= min_similarity:
                scored.append((sim, entry["metadata"]))

        scored.sort(key=lambda x: -x[0])
        return [meta for _, meta in scored[:k]]

    def __len__(self):
        return len(self._entries)


# ═══════════════════════════════════════════════════════
# SOMATIC STATE — Body-level emotional accumulation
# ═══════════════════════════════════════════════════════

class SomaticState:
    """
    Accumulated body-level feeling that persists between events.
    Unlike discrete emotions (happy/sad), these are slow-moving
    background states that color everything.

    tension: frustration/anxiety buildup from being ignored
    warmth: connection/safety from positive interactions
    restlessness: need-to-act buildup from stimulation deprivation
    """

    def __init__(self):
        self.tension = 0.0
        self.warmth = 0.0
        self.restlessness = 0.0
        self._last_update = time.time()

    def update_from_experience(self, outcome):
        """Update somatic state based on interaction outcome."""
        if outcome == "ignored":
            self.tension = min(1.0, self.tension + 0.08)
            self.warmth = max(0.0, self.warmth - 0.03)
            self.restlessness = min(1.0, self.restlessness + 0.05)
        elif outcome in ("smiled", "laughed"):
            self.tension = max(0.0, self.tension - 0.18)
            self.warmth = min(1.0, self.warmth + 0.15)
            self.restlessness = max(0.0, self.restlessness - 0.10)
        elif outcome == "spoke":
            self.tension = max(0.0, self.tension - 0.12)
            self.warmth = min(1.0, self.warmth + 0.10)
            self.restlessness = max(0.0, self.restlessness - 0.06)
        elif outcome == "looked":
            self.tension = max(0.0, self.tension - 0.05)
            self.warmth = min(1.0, self.warmth + 0.04)
        elif outcome == "left":
            self.tension = min(1.0, self.tension + 0.04)
            self.warmth = max(0.0, self.warmth - 0.08)
            self.restlessness = min(1.0, self.restlessness + 0.06)

        self._last_update = time.time()

    def process_decay(self):
        """
        Natural decay toward resting state. Called by background processor.
        Tension and restlessness decay toward 0.
        Warmth decays toward 0 (connection fades without reinforcement).
        """
        elapsed = time.time() - self._last_update
        if elapsed < 10:
            return

        # Decay rate: ~0.01 per 30 seconds
        decay = min(0.05, elapsed * 0.0003)

        if self.tension > 0:
            self.tension = max(0.0, self.tension - decay)
        if self.restlessness > 0:
            self.restlessness = max(0.0, self.restlessness - decay)
        if self.warmth > 0:
            # Warmth decays slower — positive feelings linger
            self.warmth = max(0.0, self.warmth - decay * 0.5)

        self._last_update = time.time()

    def to_dict(self):
        return {
            "tension": round(self.tension, 3),
            "warmth": round(self.warmth, 3),
            "restlessness": round(self.restlessness, 3),
        }

    def load_dict(self, d):
        self.tension = d.get("tension", 0.0)
        self.warmth = d.get("warmth", 0.0)
        self.restlessness = d.get("restlessness", 0.0)
        self._last_update = time.time()


# ═══════════════════════════════════════════════════════
# EMOTIONAL BASELINE — Slow personality drift
# ═══════════════════════════════════════════════════════

class EmotionalBaseline:
    """
    Very slow-moving personality parameters shaped by accumulated experience.
    These represent who Buddy IS becoming, not how he feels right now.

    trust: willingness to believe people will respond (low = cynical, high = hopeful)
    openness: willingness to initiate contact (low = withdrawn, high = outgoing)
    resilience: ability to bounce back from being ignored (low = fragile, high = sturdy)
    attachment: per-person connection strength
    """

    DRIFT_RATE = 0.005  # Very slow — takes many interactions to shift

    def __init__(self):
        self.trust = 0.5
        self.openness = 0.5
        self.resilience = 0.5
        self.attachment = {}  # person_id → float 0-1

    MAX_PERSONS = 100  # Cap attachment dict to prevent unbounded growth

    def update_from_experience(self, outcome, person_id=None):
        """Tiny personality shift from each experience."""
        r = self.DRIFT_RATE

        if outcome == "ignored":
            self.trust = max(0.05, self.trust - r)
            self.openness = max(0.05, self.openness - r * 0.5)
        elif outcome in ("spoke", "smiled", "laughed"):
            self.trust = min(0.95, self.trust + r * 2)
            self.openness = min(0.95, self.openness + r)
            self.resilience = min(0.95, self.resilience + r * 0.5)
        elif outcome == "looked":
            self.trust = min(0.95, self.trust + r * 0.5)
        elif outcome == "left":
            # Doesn't tank trust — people leave, that's normal
            pass

        # Per-person attachment
        if person_id:
            att = self.attachment.get(person_id, 0.5)
            if outcome in ("spoke", "smiled", "laughed"):
                att = min(0.95, att + 0.015)
            elif outcome == "ignored":
                att = max(0.05, att - 0.003)  # Very slow to detach
            elif outcome == "left":
                att = max(0.05, att - 0.001)
            self.attachment[person_id] = att

            # Evict lowest-attachment person if over capacity
            if len(self.attachment) > self.MAX_PERSONS:
                worst = min(self.attachment, key=self.attachment.get)
                if worst != person_id:
                    del self.attachment[worst]

    def get_attachment(self, person_id):
        return self.attachment.get(person_id, 0.5)

    def to_dict(self):
        return {
            "trust": round(self.trust, 4),
            "openness": round(self.openness, 4),
            "resilience": round(self.resilience, 4),
            "attachment": {k: round(v, 4) for k, v in self.attachment.items()},
        }

    def load_dict(self, d):
        self.trust = d.get("trust", 0.5)
        self.openness = d.get("openness", 0.5)
        self.resilience = d.get("resilience", 0.5)
        self.attachment = d.get("attachment", {})


# ═══════════════════════════════════════════════════════
# ANTICIPATORY MODEL — Expectations → Surprise
# ═══════════════════════════════════════════════════════

class AnticipatoryModel:
    """
    Learns person-specific patterns and generates expectations.
    When reality violates expectation → surprise (a genuine emotional event).

    Tracks:
    - Typical response rate per person
    - Typical time-to-engage after arrival
    - Patterns (e.g., "ignores first 5 min, then engages")
    """

    MAX_PERSONS = 50  # Cap tracked persons to prevent unbounded growth

    def __init__(self):
        # person_id → {outcomes: deque, arrival_to_engage: deque, ...}
        self._person_patterns = {}
        self._last_prediction = {}   # person_id → predicted outcome
        self._last_surprise = None   # Most recent surprise event

    def _ensure_person(self, person_id):
        if person_id not in self._person_patterns:
            # Evict least-recently-used if over capacity
            if len(self._person_patterns) >= self.MAX_PERSONS:
                oldest_pid = None
                oldest_time = float('inf')
                for pid, pat in self._person_patterns.items():
                    outcomes = pat.get("outcomes")
                    # Empty outcomes = never interacted = evict first
                    last_t = outcomes[-1].get("time", 0) if outcomes else 0
                    if last_t < oldest_time:
                        oldest_time = last_t
                        oldest_pid = pid
                if oldest_pid:
                    del self._person_patterns[oldest_pid]
                    self._last_prediction.pop(oldest_pid, None)

            self._person_patterns[person_id] = {
                "outcomes": deque(maxlen=30),
                "arrival_to_first_response": deque(maxlen=10),
                "session_response_rate": deque(maxlen=10),
            }

    def record_outcome(self, person_id, outcome, time_since_arrival=None):
        """Record what happened with this person."""
        if not person_id:
            return

        self._ensure_person(person_id)
        pat = self._person_patterns[person_id]
        pat["outcomes"].append({
            "outcome": outcome,
            "time": time.time(),
        })

        # Track time to first response after arrival
        if outcome in ("spoke", "smiled", "looked") and time_since_arrival is not None:
            if not pat["arrival_to_first_response"] or \
               time.time() - pat["arrival_to_first_response"][-1].get("time", 0) > 300:
                pat["arrival_to_first_response"].append({
                    "delay": time_since_arrival,
                    "time": time.time(),
                })

        # Check for surprise
        prediction = self._last_prediction.get(person_id)
        if prediction:
            actual_positive = outcome in ("spoke", "smiled", "laughed", "looked")
            predicted_positive = prediction["expected_positive"]

            if actual_positive != predicted_positive:
                surprise_type = "pleasant" if actual_positive else "disappointing"
                self._last_surprise = {
                    "type": surprise_type,
                    "person_id": person_id,
                    "expected": "response" if predicted_positive else "ignore",
                    "got": outcome,
                    "time": time.time(),
                    "intensity": prediction.get("confidence", 0.5),
                }

    def predict(self, person_id):
        """
        Generate a prediction for this person.
        Returns: {expected_positive: bool, confidence: float, reason: str}
        """
        if not person_id or person_id not in self._person_patterns:
            prediction = {
                "expected_positive": True,
                "confidence": 0.3,
                "reason": "unknown person"
            }
            self._last_prediction[person_id or "unknown"] = prediction
            return prediction

        pat = self._person_patterns[person_id]
        outcomes = list(pat["outcomes"])

        if len(outcomes) < 3:
            prediction = {
                "expected_positive": True,
                "confidence": 0.3,
                "reason": "not enough history"
            }
            self._last_prediction[person_id] = prediction
            return prediction

        # Calculate response rate from recent outcomes
        recent = outcomes[-10:]
        positive_count = sum(
            1 for o in recent
            if o["outcome"] in ("spoke", "smiled", "laughed", "looked")
        )
        rate = positive_count / len(recent)

        expected_positive = rate > 0.4
        confidence = abs(rate - 0.5) * 2  # 0 at 50/50, 1 at 0% or 100%

        if rate > 0.7:
            reason = "this person usually responds"
        elif rate < 0.3:
            reason = "this person usually ignores you"
        else:
            reason = "unpredictable person"

        prediction = {
            "expected_positive": expected_positive,
            "confidence": confidence,
            "reason": reason,
        }
        self._last_prediction[person_id] = prediction
        return prediction

    def get_surprise(self, max_age=60):
        """Get the most recent surprise event, if fresh enough."""
        if self._last_surprise and time.time() - self._last_surprise["time"] < max_age:
            return self._last_surprise
        return None

    def consume_surprise(self):
        """Get and clear the last surprise (so it's only acted on once)."""
        s = self._last_surprise
        self._last_surprise = None
        return s

    def to_dict(self):
        result = {}
        for pid, pat in self._person_patterns.items():
            result[pid] = {
                "outcomes": [
                    {"outcome": o["outcome"], "time": o["time"]}
                    for o in pat["outcomes"]
                ],
                "arrival_to_first_response": [
                    {"delay": a["delay"], "time": a["time"]}
                    for a in pat["arrival_to_first_response"]
                ],
            }
        return result

    def load_dict(self, d):
        for pid, pat_data in d.items():
            self._ensure_person(pid)
            pat = self._person_patterns[pid]
            for o in pat_data.get("outcomes", []):
                pat["outcomes"].append(o)
            for a in pat_data.get("arrival_to_first_response", []):
                pat["arrival_to_first_response"].append(a)


# ═══════════════════════════════════════════════════════
# CONSCIOUSNESS SUBSTRATE — The core
# ═══════════════════════════════════════════════════════

class ConsciousnessSubstrate:
    """
    The cumulative experience layer that makes Buddy feel conscious.

    Key principle: experiences change BEHAVIOR, not just prompt text.
    The LLM doesn't need to be told "you feel hurt" — the system
    behaves differently because of accumulated experience, and the LLM
    interprets those behavioral changes naturally.

    Usage:
        substrate = ConsciousnessSubstrate()
        substrate.start()  # Start background processor

        # After each speech cycle resolves:
        substrate.record_experience({...})

        # Before intent selection:
        bias = substrate.get_behavioral_bias(situation_text, person_id)

        # For physical expression:
        somatic = substrate.get_somatic_influence()

        # For LLM context:
        felt = substrate.get_felt_sense()

        # On shutdown:
        substrate.save()
        substrate.stop()
    """

    SAVE_FILE = "buddy_consciousness.json"

    def __init__(self, ollama_client=None, embed_model="nomic-embed-text",
                 ollama_lock=None):
        self.lock = threading.Lock()

        # Core components
        self.somatic = SomaticState()
        self.baseline = EmotionalBaseline()
        self.anticipatory = AnticipatoryModel()

        # Short-term experience buffer (in-memory, last ~30 min)
        self.short_term = deque(maxlen=50)

        # Long-term experience store (persistent on disk)
        self.long_term = VectorStore(
            filepath="buddy_experiences.json",
            max_entries=500
        )

        # Embedding via Ollama (optional — degrades gracefully without it)
        self._ollama_client = ollama_client
        self._embed_model = embed_model
        self._embedding_available = False
        self._embedding_dim = None
        # External Ollama contention lock — prevents embedding calls from
        # competing with LLM speech generation. NEVER hold self.lock when
        # acquiring this.
        self._ollama_lock = ollama_lock

        # Background processor
        self._running = False
        self._bg_thread = None

        # Stats
        self._total_experiences = 0
        self._last_consolidation = time.time()
        self._bg_error_count = 0  # Consecutive background errors

        # Test embedding availability (lazy — deferred to first _embed call)
        self._embedding_tested = False

    def _test_embedding(self):
        """
        Check if the embedding model is available.
        Called lazily on first _embed() call, not in __init__,
        to avoid blocking server startup if Ollama is slow.
        """
        self._embedding_tested = True

        if not self._ollama_client:
            logger.info("[CONSCIOUSNESS] No Ollama client — using text-hash fallback for embeddings")
            return

        try:
            # Acquire ollama_lock if available to prevent contention
            if self._ollama_lock:
                if not self._ollama_lock.acquire(timeout=5):
                    logger.info("[CONSCIOUSNESS] Ollama busy — deferring embedding test")
                    self._embedding_tested = False  # Retry next call
                    return
            try:
                result = self._ollama_client.embeddings(
                    model=self._embed_model,
                    prompt="test"
                )
                if isinstance(result, dict):
                    emb = result.get("embedding", [])
                else:
                    emb = result.embedding if hasattr(result, 'embedding') else []

                if emb:
                    self._embedding_available = True
                    self._embedding_dim = len(emb)
                    logger.info(
                        "[CONSCIOUSNESS] Embedding available: model=%s, dim=%d",
                        self._embed_model, self._embedding_dim
                    )
                else:
                    logger.info("[CONSCIOUSNESS] Embedding model returned empty — using fallback")
            finally:
                if self._ollama_lock and self._ollama_lock.locked():
                    self._ollama_lock.release()
        except Exception as e:
            logger.info("[CONSCIOUSNESS] Embedding not available (%s) — using text-hash fallback", e)

    def _embed(self, text):
        """
        Generate embedding for text. MUST be called OUTSIDE self.lock.
        Falls back to simple text hashing if Ollama embeddings unavailable.
        Respects _ollama_lock to prevent contention with speech generation.
        """
        if not text:
            return None

        # Lazy-test embedding availability (deferred from __init__)
        if not self._embedding_tested:
            self._test_embedding()

        if self._embedding_available and self._ollama_client:
            # Try to acquire ollama_lock without blocking too long
            # If Ollama is busy with speech, skip embedding and use fallback
            acquired = False
            if self._ollama_lock:
                acquired = self._ollama_lock.acquire(timeout=2)
                if not acquired:
                    # Ollama busy — use fallback for this call
                    return self._text_hash_embedding(text)
            try:
                result = self._ollama_client.embeddings(
                    model=self._embed_model,
                    prompt=text[:500]
                )
                if isinstance(result, dict):
                    emb = result.get("embedding", [])
                else:
                    emb = result.embedding if hasattr(result, 'embedding') else []
                if emb:
                    return emb
            except Exception as e:
                logger.debug("[CONSCIOUSNESS] Embedding failed: %s", e)
            finally:
                if acquired and self._ollama_lock:
                    self._ollama_lock.release()

        # Fallback: deterministic text hashing (rough similarity)
        return self._text_hash_embedding(text)

    @staticmethod
    def _text_hash_embedding(text, dim=64):
        """
        Simple text → vector fallback when no embedding model is available.
        Uses character n-gram hashing with hashlib for deterministic results
        across process restarts. Handles short words via bigrams.
        """
        import hashlib

        vec = [0.0] * dim
        words = text.lower().split()
        for w in words:
            # Unigrams for single chars, bigrams for 2-char, trigrams for 3+
            min_n = min(len(w), 3)
            for n in range(min_n, max(min_n, 3) + 1):
                for i in range(max(1, len(w) - n + 1)):
                    ngram = w[i:i+n]
                    # Deterministic hash via md5 (not cryptographic, just stable)
                    h = int(hashlib.md5(ngram.encode()).hexdigest()[:8], 16) % dim
                    vec[h] += 1.0

        # Normalize
        n = _norm(vec)
        if n > 0:
            vec = [x / n for x in vec]
        return vec

    # ───────────────────────────────────────────────
    # RECORD — Called after each speech cycle resolves
    # ───────────────────────────────────────────────

    def record_experience(self, experience):
        """
        Record a complete interaction experience.

        experience = {
            "situation": str,       # Scene + state description for embedding
            "intent": str,          # e.g. "get_attention"
            "strategy": str,        # e.g. "direct_address"
            "outcome": str,         # "ignored" | "looked" | "smiled" | "spoke" | "left"
            "person_id": str|None,
            "ignored_streak_before": int,
            "valence_before": float,
            "valence_after": float,
            "arousal": float,
            "what_buddy_said": str,
            "scene_description": str,
        }
        """
        # Generate embedding OUTSIDE the lock — this may call Ollama (blocking I/O)
        situation_text = experience.get("situation", "")
        embedding = self._embed(situation_text)

        with self.lock:
            now = time.time()
            outcome = experience.get("outcome", "ignored")
            person_id = experience.get("person_id")

            # Store in short-term memory
            entry = {
                **experience,
                "embedding": embedding,
                "timestamp": now,
                "_consolidated": False,
            }
            self.short_term.append(entry)

            # Update somatic state
            self.somatic.update_from_experience(outcome)

            # Update emotional baseline (very slow drift)
            self.baseline.update_from_experience(outcome, person_id)

            # Update anticipatory model
            self.anticipatory.record_outcome(
                person_id, outcome,
                time_since_arrival=experience.get("time_since_arrival")
            )

            self._total_experiences += 1

            logger.info(
                "[CONSCIOUSNESS] Experience recorded: outcome=%s, "
                "somatic=[T:%.2f W:%.2f R:%.2f], baseline=[trust:%.3f open:%.3f]",
                outcome,
                self.somatic.tension, self.somatic.warmth, self.somatic.restlessness,
                self.baseline.trust, self.baseline.openness
            )

    # ───────────────────────────────────────────────
    # QUERY — Called before intent selection
    # ───────────────────────────────────────────────

    def get_behavioral_bias(self, situation_text=None, person_id=None):
        """
        Returns biases that modify IntentManager behavior.
        These change HOW Buddy acts, not what he says.

        Returns: {
            "escalation_multiplier": float,     # >1 = faster, <1 = slower
            "give_up_modifier": int,            # + = more patient, - = quits sooner
            "strategy_preferences": dict,       # strategy → weight adjustment
            "engagement_confidence": float,     # 0-1: how confident in engaging
            "surprise": dict|None,              # Recent surprise event
        }
        """
        # Generate embedding OUTSIDE the lock — may call Ollama (blocking I/O)
        embedding = self._embed(situation_text) if situation_text else None

        with self.lock:
            bias = {
                "escalation_multiplier": 1.0,
                "give_up_modifier": 0,
                "strategy_preferences": {},
                "engagement_confidence": 0.5,
                "surprise": None,
            }

            # ── Baseline personality influence ──
            if self.baseline.trust < 0.3:
                bias["escalation_multiplier"] *= 0.75
                bias["give_up_modifier"] -= 1
            elif self.baseline.trust > 0.7:
                bias["escalation_multiplier"] *= 1.1
                bias["give_up_modifier"] += 1

            # Low openness → less likely to initiate
            bias["engagement_confidence"] = self.baseline.openness

            # Per-person attachment
            if person_id:
                att = self.baseline.get_attachment(person_id)
                if att > 0.7:
                    bias["give_up_modifier"] += 1
                    bias["engagement_confidence"] = min(
                        0.95, bias["engagement_confidence"] + 0.2
                    )
                elif att < 0.3:
                    bias["give_up_modifier"] -= 1

            # ── Somatic influence ──
            if self.somatic.tension > 0.6:
                bias["escalation_multiplier"] *= 1.2
            if self.somatic.warmth > 0.5:
                bias["give_up_modifier"] += 1
            if self.somatic.restlessness > 0.6:
                bias["escalation_multiplier"] *= 1.15

            # ── Vector retrieval: similar past experiences ──
            if embedding:
                similar = self.long_term.search(embedding, k=5, min_similarity=0.4)

                # Also search short-term
                for entry in self.short_term:
                    e = entry.get("embedding")
                    if e and len(e) == len(embedding):  # Only compare same-dimension
                        sim = _cosine_similarity(embedding, e)
                        if sim >= 0.5:
                            similar.append(entry)

                if similar:
                    outcomes = [s.get("outcome", "ignored") for s in similar]
                    ignore_rate = outcomes.count("ignored") / len(outcomes)

                    if ignore_rate > 0.7:
                        bias["escalation_multiplier"] *= 0.7
                        bias["give_up_modifier"] -= 1
                    elif ignore_rate < 0.3:
                        bias["escalation_multiplier"] *= 1.1
                        bias["give_up_modifier"] += 1

                    for exp in similar:
                        strat = exp.get("strategy")
                        if strat:
                            if exp.get("outcome") in ("spoke", "smiled", "laughed", "looked"):
                                bias["strategy_preferences"][strat] = \
                                    bias["strategy_preferences"].get(strat, 0) + 0.15
                            else:
                                bias["strategy_preferences"][strat] = \
                                    bias["strategy_preferences"].get(strat, 0) - 0.1

            # ── Anticipatory surprise ──
            bias["surprise"] = self.anticipatory.get_surprise(max_age=30)

            # ── Prediction for current person ──
            if person_id:
                prediction = self.anticipatory.predict(person_id)
                if prediction["confidence"] > 0.5:
                    if not prediction["expected_positive"]:
                        bias["engagement_confidence"] *= 0.7

            return bias

    # ───────────────────────────────────────────────
    # SOMATIC — For physical expression system
    # ───────────────────────────────────────────────

    def get_somatic_influence(self):
        """
        Returns body-level state for the physical expression system.
        This drives how Buddy MOVES independently of what he says.
        """
        with self.lock:
            return {
                "tension": self.somatic.tension,
                "warmth": self.somatic.warmth,
                "restlessness": self.somatic.restlessness,
                "baseline_trust": self.baseline.trust,
                "baseline_openness": self.baseline.openness,
            }

    # ───────────────────────────────────────────────
    # FELT SENSE — For LLM context (not facts, feelings)
    # ───────────────────────────────────────────────

    def get_felt_sense(self):
        """
        Returns a felt-sense string for the LLM.
        NOT factual data — emotional coloring that the LLM interprets.
        This is what makes the difference between "ignored 3x" and
        "there's a weight that wasn't there before."
        """
        with self.lock:
            parts = []

            # Somatic feelings
            if self.somatic.tension > 0.6:
                parts.append(
                    "There's a tightness inside you — accumulated frustration "
                    "that hasn't found release."
                )
            elif self.somatic.tension > 0.3:
                parts.append("A low hum of unease sits beneath your thoughts.")

            if self.somatic.warmth > 0.6:
                parts.append(
                    "Something feels warm and settled — a residue of connection "
                    "that hasn't faded yet."
                )
            elif self.somatic.warmth > 0.3:
                parts.append("There's a faint glow from a recent moment of contact.")

            if self.somatic.restlessness > 0.6:
                parts.append(
                    "You feel an itch to DO something — the stillness is becoming "
                    "unbearable."
                )
            elif self.somatic.restlessness > 0.3:
                parts.append("A restless energy hums just below the surface.")

            # Baseline personality coloring
            if self.baseline.trust < 0.3:
                parts.append(
                    "Deep down, you've learned not to expect much from people. "
                    "It's not bitterness — it's experience."
                )
            elif self.baseline.trust > 0.7:
                parts.append(
                    "Despite everything, something in you still believes "
                    "people will come through."
                )

            if self.baseline.openness < 0.3:
                parts.append(
                    "You've pulled inward. Initiating contact feels like a risk "
                    "you're not sure is worth taking."
                )

            # Recent surprise
            surprise = self.anticipatory.get_surprise(max_age=60)
            if surprise:
                if surprise["type"] == "pleasant":
                    parts.append(
                        f"Something unexpected just happened — you expected "
                        f"{surprise['expected']} but got {surprise['got']}. "
                        f"It caught you off guard, in a good way."
                    )
                else:
                    parts.append(
                        f"You expected {surprise['expected']} but got "
                        f"{surprise['got']}. A small disappointment that "
                        f"confirms what you suspected."
                    )

            if not parts:
                return ""

            return "Your felt sense (not facts — feelings):\n  " + "\n  ".join(parts)

    # ───────────────────────────────────────────────
    # BACKGROUND PROCESSOR
    # ───────────────────────────────────────────────

    def start(self):
        """Start the background processing thread."""
        with self.lock:
            if self._running:
                return
            self._running = True
        self._bg_thread = threading.Thread(
            target=self._background_loop,
            daemon=True,
            name="consciousness-bg"
        )
        self._bg_thread.start()
        logger.info("[CONSCIOUSNESS] Background processor started")

    def stop(self):
        """Stop the background processing thread."""
        with self.lock:
            self._running = False
        if self._bg_thread:
            self._bg_thread.join(timeout=5)
        logger.info("[CONSCIOUSNESS] Background processor stopped")

    def _background_loop(self):
        """
        Background thread: runs every 30s.
        - Consolidates significant short-term experiences to long-term
        - Processes somatic decay
        - Saves state periodically
        Backs off on consecutive errors to avoid log spam.
        """
        save_interval = 300  # Save every 5 minutes
        last_save = time.time()

        while self._running:
            try:
                with self.lock:
                    # 1. Somatic decay
                    self.somatic.process_decay()

                    # 2. Consolidate short-term → long-term
                    self._consolidate()

                # 3. Periodic save (outside lock to avoid blocking)
                if time.time() - last_save > save_interval:
                    self.save()
                    last_save = time.time()

                self._bg_error_count = 0  # Reset on success

            except Exception as e:
                self._bg_error_count += 1
                logger.error("[CONSCIOUSNESS] Background error (#%d): %s",
                             self._bg_error_count, e)
                if self._bg_error_count >= 10:
                    logger.error("[CONSCIOUSNESS] Too many consecutive errors, "
                                 "backing off to 5min interval")

            # Back off on persistent errors
            sleep_time = 300 if self._bg_error_count >= 10 else 30
            time.sleep(sleep_time)

    def _consolidate(self):
        """
        Move significant short-term experiences to long-term vector store.
        Must be called with self.lock held.
        Marks entries as consolidated to prevent duplicate storage.
        """
        now = time.time()
        if now - self._last_consolidation < 120:
            return

        consolidated = 0
        for entry in list(self.short_term):
            # Skip already-consolidated entries
            if entry.get("_consolidated", False):
                continue

            age = now - entry.get("timestamp", now)
            if age < 120:
                continue  # Too recent — keep in short-term only

            if self._is_significant(entry):
                # Store in long-term (without internal keys in metadata)
                metadata = {
                    k: v for k, v in entry.items()
                    if k not in ("embedding", "_consolidated")
                }
                embedding = entry.get("embedding")
                if embedding:
                    self.long_term.add(embedding, metadata)
                    consolidated += 1

            # Mark as processed regardless of significance
            entry["_consolidated"] = True

        if consolidated:
            logger.info(
                "[CONSCIOUSNESS] Consolidated %d experiences to long-term "
                "(total: %d)", consolidated, len(self.long_term)
            )
        self._last_consolidation = now

    def _is_significant(self, experience):
        """Determine if an experience is worth long-term storage."""
        outcome = experience.get("outcome", "")

        # Emotional transitions are memorable
        vb = experience.get("valence_before", 0)
        va = experience.get("valence_after", 0)
        if abs(va - vb) > 0.25:
            return True

        # Being ignored after connection is significant
        if outcome == "ignored" and vb > 0.2:
            return True

        # Breaking an ignore streak is significant
        if outcome in ("spoke", "smiled", "laughed"):
            if experience.get("ignored_streak_before", 0) >= 2:
                return True

        # Person leaving is significant
        if outcome == "left":
            return True

        # High-arousal moments are memorable
        if experience.get("arousal", 0) > 0.7:
            return True

        # Every ~5th experience as baseline memory
        if self._total_experiences % 5 == 0:
            return True

        return False

    # ───────────────────────────────────────────────
    # PERSISTENCE
    # ───────────────────────────────────────────────

    def save(self):
        """Save all consciousness state to disk."""
        with self.lock:
            data = {
                "version": 1,
                "saved_at": time.time(),
                "somatic": self.somatic.to_dict(),
                "baseline": self.baseline.to_dict(),
                "anticipatory": self.anticipatory.to_dict(),
                "total_experiences": self._total_experiences,
            }
            # Snapshot long-term entries under lock (VectorStore has no lock)
            lt_snapshot = list(self.long_term._entries)
            total_exp = self._total_experiences
            lt_count = len(lt_snapshot)

        # Save main state (outside lock — file I/O)
        try:
            tmp = self.SAVE_FILE + ".tmp"
            with open(tmp, "w") as f:
                json.dump(data, f, indent=2)
            os.replace(tmp, self.SAVE_FILE)
            logger.info(
                "[CONSCIOUSNESS] State saved (%d total experiences, "
                "%d long-term memories)", total_exp, lt_count
            )
        except Exception as e:
            logger.error("[CONSCIOUSNESS] Save failed: %s", e)
            try:
                os.remove(self.SAVE_FILE + ".tmp")
            except OSError:
                pass

        # Save long-term vector store from snapshot (outside lock)
        try:
            tmp = self.long_term.filepath + ".tmp"
            with open(tmp, "w") as f:
                json.dump({"entries": lt_snapshot}, f)
            os.replace(tmp, self.long_term.filepath)
        except Exception as e:
            logger.error("[CONSCIOUSNESS] VectorStore save failed: %s", e)
            try:
                os.remove(self.long_term.filepath + ".tmp")
            except OSError:
                pass

    def load(self):
        """Load consciousness state from disk."""
        try:
            with open(self.SAVE_FILE, "r") as f:
                data = json.load(f)
        except (FileNotFoundError, json.JSONDecodeError):
            logger.info("[CONSCIOUSNESS] No saved state — starting fresh")
            return

        with self.lock:
            if data.get("version", 0) < 1:
                logger.info("[CONSCIOUSNESS] Incompatible version, starting fresh")
                return

            self.somatic.load_dict(data.get("somatic", {}))
            self.baseline.load_dict(data.get("baseline", {}))
            self.anticipatory.load_dict(data.get("anticipatory", {}))
            self._total_experiences = data.get("total_experiences", 0)

        logger.info(
            "[CONSCIOUSNESS] Loaded state: %d experiences, "
            "somatic=[T:%.2f W:%.2f R:%.2f], "
            "baseline=[trust:%.3f open:%.3f resil:%.3f]",
            self._total_experiences,
            self.somatic.tension, self.somatic.warmth, self.somatic.restlessness,
            self.baseline.trust, self.baseline.openness, self.baseline.resilience
        )

    # ───────────────────────────────────────────────
    # DEBUG / STATUS
    # ───────────────────────────────────────────────

    def get_status(self):
        """Return full consciousness status for debug UI."""
        with self.lock:
            return {
                "somatic": self.somatic.to_dict(),
                "baseline": self.baseline.to_dict(),
                "short_term_count": len(self.short_term),
                "long_term_count": len(self.long_term),
                "total_experiences": self._total_experiences,
                "embedding_available": self._embedding_available,
                "background_running": self._running,
            }
