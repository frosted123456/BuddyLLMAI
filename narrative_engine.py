"""
narrative_engine.py — Buddy's Narrative Continuity Engine
=========================================================

Maintains Buddy's "inner story" — a persistent state that creates the
illusion of a mind existing between observed moments. Every LLM call
receives this full narrative context so Buddy can reference what he said
before, notice if he's being ignored, escalate strategies, and show subtext.

This is the core of "The Spark" — what makes Buddy feel genuinely alive
rather than a collection of periodic systems.
"""

import os
import time
import threading
import json
from collections import deque, defaultdict


class NarrativeEngine:
    """
    Maintains Buddy's continuous inner narrative across utterances.

    Key responsibilities:
    - Track what Buddy has said (utterance history with response tracking)
    - Maintain conversational threads (topics Buddy brought up)
    - Build a mood narrative (WHY Buddy feels how he feels, not just numbers)
    - Track human responsiveness (are they engaging, ignoring, absent?)
    - Log notable events with Buddy's emotional reactions
    """

    def __init__(self, max_utterances=10, max_threads=5, max_events=15):
        self.lock = threading.Lock()

        # ── Utterance History ──
        # What Buddy has said, with timestamps and response tracking
        self.utterance_history = deque(maxlen=max_utterances)

        # ── Open Threads ──
        # Topics Buddy brought up that weren't acknowledged
        self.open_threads = deque(maxlen=max_threads)

        # ── Human Responsiveness ──
        self.human_responsiveness = {
            "last_interaction": 0,        # timestamp of last human→Buddy interaction
            "last_verbal_response": 0,    # timestamp of last speech from human
            "responses_to_last_5": 0,     # how many of last 5 utterances got a response
            "attention_level": "unknown",  # low / medium / high / unknown
            "pattern": "unknown",          # engaging / distracted / mostly_ignoring / absent
            "ignored_streak": 0,           # consecutive utterances with no response
            "face_present": False,
            "face_present_since": 0,
            "face_absent_since": 0,
        }

        # ── Mood Narrative ──
        # A human-readable explanation of WHY Buddy feels how he feels
        self.mood_narrative = ""
        self._last_mood_update = 0

        # ── Recent Notable Events ──
        self.recent_events = deque(maxlen=max_events)

        # ── Object Memory ──
        # Tracks objects across scene descriptions for persistent reference
        self.object_memory = {}  # name → {first_seen, last_seen, times_seen, mentioned_by_buddy, state}

        # ── Person Profiles ──
        # Tracks per-person behavioral patterns across interactions
        self.person_profiles = {}  # person_id → profile dict
        self._current_person_id = None

        # ── Conversation state ──
        self.last_speech_time = 0
        self.total_utterances_session = 0
        self.session_start = time.time()

    # ═══════════════════════════════════════════════════════
    # UTTERANCE TRACKING
    # ═══════════════════════════════════════════════════════

    def record_utterance(self, text, trigger="spontaneous", intent=None):
        """Record something Buddy said."""
        with self.lock:
            now = time.time()
            entry = {
                "text": text,
                "time": now,
                "trigger": trigger,
                "intent": intent,
                "response": "pending",   # pending → responded / ignored / no_one_present
                "response_type": None,    # looked / smiled / spoke / laughed / none
                "response_time": None,
            }
            self.utterance_history.append(entry)
            self.last_speech_time = now
            self.total_utterances_session += 1

            # Extract topic keywords for thread tracking
            self._update_threads(text, now)

    def record_response(self, response_type="verbal"):
        """
        Record that the human responded to Buddy's last utterance.

        response_type: "looked" / "smiled" / "spoke" / "laughed" / "approached"
        """
        with self.lock:
            now = time.time()

            # Mark the most recent pending utterance as responded
            for entry in reversed(self.utterance_history):
                if entry["response"] == "pending":
                    entry["response"] = "responded"
                    entry["response_type"] = response_type
                    entry["response_time"] = now
                    break

            # Update responsiveness
            self.human_responsiveness["last_interaction"] = now
            if response_type in ("spoke", "laughed"):
                self.human_responsiveness["last_verbal_response"] = now
            self.human_responsiveness["ignored_streak"] = 0
            self._recalculate_responsiveness()

    def record_ignored(self):
        """Mark that the most recent utterance was ignored (timeout)."""
        with self.lock:
            for entry in reversed(self.utterance_history):
                if entry["response"] == "pending":
                    entry["response"] = "ignored"
                    entry["response_type"] = "none"
                    break

            self.human_responsiveness["ignored_streak"] += 1
            self._recalculate_responsiveness()

    def record_human_speech(self):
        """Record that the human spoke (even if not in response to Buddy)."""
        with self.lock:
            now = time.time()
            self.human_responsiveness["last_interaction"] = now
            self.human_responsiveness["last_verbal_response"] = now
            self.human_responsiveness["ignored_streak"] = max(
                0, self.human_responsiveness["ignored_streak"] - 1
            )
            self._recalculate_responsiveness()

    # ═══════════════════════════════════════════════════════
    # THREAD TRACKING
    # ═══════════════════════════════════════════════════════

    def _update_threads(self, text, now):
        """Extract and track conversational topics from Buddy's speech."""
        text_lower = text.lower()

        # Simple keyword-based topic extraction
        topic_keywords = {
            "mug": ["mug", "cup", "coffee", "tea"],
            "person": ["you", "someone", "person", "they"],
            "desk": ["desk", "table", "surface"],
            "monitor": ["monitor", "screen", "display"],
            "phone": ["phone", "device"],
            "book": ["book", "reading"],
            "quiet": ["quiet", "silence", "silent"],
            "alone": ["alone", "lonely", "nobody", "empty"],
            "time": ["morning", "afternoon", "evening", "time", "hour"],
            "weather": ["light", "dark", "shadow", "bright"],
        }

        detected_topics = []
        for topic, keywords in topic_keywords.items():
            if any(kw in text_lower for kw in keywords):
                detected_topics.append(topic)

        for topic in detected_topics:
            # Check if this topic is already being tracked
            existing = None
            for thread in self.open_threads:
                if thread["topic"] == topic:
                    existing = thread
                    break

            if existing:
                existing["times_mentioned"] += 1
                existing["last_mentioned"] = now
            else:
                self.open_threads.append({
                    "topic": topic,
                    "first_mentioned": now,
                    "last_mentioned": now,
                    "times_mentioned": 1,
                    "acknowledged": False,
                })

    def acknowledge_thread(self, topic):
        """Mark a thread as acknowledged (human responded to it)."""
        with self.lock:
            for thread in self.open_threads:
                if thread["topic"] == topic:
                    thread["acknowledged"] = True
                    break

    # ═══════════════════════════════════════════════════════
    # HUMAN RESPONSIVENESS
    # ═══════════════════════════════════════════════════════

    def update_face_state(self, face_present):
        """Update face presence tracking."""
        with self.lock:
            now = time.time()
            was_present = self.human_responsiveness["face_present"]

            if face_present and not was_present:
                self.human_responsiveness["face_present_since"] = now
                self.human_responsiveness["face_present"] = True
                self.recent_events.append({
                    "event": "person_appeared",
                    "time": now,
                    "buddy_reaction": "noticed"
                })
            elif not face_present and was_present:
                self.human_responsiveness["face_absent_since"] = now
                self.human_responsiveness["face_present"] = False
                self.recent_events.append({
                    "event": "person_left",
                    "time": now,
                    "buddy_reaction": "noticed_absence"
                })

            self._recalculate_responsiveness()

    def _recalculate_responsiveness(self):
        """Recalculate attention_level and pattern from recent data."""
        now = time.time()
        hr = self.human_responsiveness

        # Count responses to last 5 utterances
        recent = list(self.utterance_history)[-5:]
        responded = sum(1 for u in recent if u["response"] == "responded")
        hr["responses_to_last_5"] = responded

        # Determine attention level
        if not hr["face_present"]:
            hr["attention_level"] = "absent"
        elif responded >= 3:
            hr["attention_level"] = "high"
        elif responded >= 1:
            hr["attention_level"] = "medium"
        else:
            hr["attention_level"] = "low"

        # Determine pattern
        if not hr["face_present"]:
            absent_for = now - hr.get("face_absent_since", now)
            if absent_for > 300:
                hr["pattern"] = "absent"
            else:
                hr["pattern"] = "just_left"
        elif hr["ignored_streak"] >= 3:
            hr["pattern"] = "mostly_ignoring"
        elif hr["ignored_streak"] >= 1 and responded < 2:
            hr["pattern"] = "distracted"
        elif responded >= 2:
            hr["pattern"] = "engaging"
        else:
            hr["pattern"] = "present"

    # ═══════════════════════════════════════════════════════
    # MOOD NARRATIVE
    # ═══════════════════════════════════════════════════════

    def update_mood_narrative(self, teensy_state, scene_context_summary=""):
        """
        Build a structured mood summary from all available context.
        Called periodically (every ~5 seconds) or after significant events.

        Uses factual/data-like descriptions — NOT prose the LLM might echo.
        The LLM should create its own inner voice from these facts.
        """
        with self.lock:
            now = time.time()
            self._last_mood_update = now
            hr = self.human_responsiveness

            parts = []

            # Emotional state as data
            emotion = teensy_state.get("emotion", "NEUTRAL")
            arousal = float(teensy_state.get("arousal", 0.5))
            valence = float(teensy_state.get("valence", 0.0))
            social = float(teensy_state.get("social", 0.5))
            energy = float(teensy_state.get("energy", 0.7))
            stimulation = float(teensy_state.get("stimulation", 0.5))

            # Mood cause — factual, not narrative
            if valence > 0.3:
                cause = "conversation" if hr["pattern"] == "engaging" else (
                    "company" if hr["face_present"] else "internal"
                )
                parts.append(f"Mood: positive (cause: {cause})")
            elif valence < -0.2:
                if hr["pattern"] == "mostly_ignoring":
                    parts.append("Mood: deflated (cause: being ignored)")
                elif hr["pattern"] == "absent":
                    gone = int(now - hr.get("face_absent_since", now))
                    parts.append(f"Mood: low (cause: alone {gone // 60}min)")
                elif social > 0.6:
                    parts.append("Mood: low (cause: social need unmet)")
                else:
                    parts.append("Mood: low (cause: unclear)")
            else:
                if arousal > 0.6:
                    parts.append("Mood: neutral-alert, watchful")
                elif energy < 0.3:
                    parts.append("Mood: neutral-low, energy depleted")
                else:
                    parts.append("Mood: neutral")

            # Social facts
            if hr["ignored_streak"] >= 3:
                parts.append(
                    f"Ignored {hr['ignored_streak']}x consecutively, zero responses"
                )
            elif hr["ignored_streak"] >= 1:
                parts.append("Last utterance: no response")

            if hr["face_present"]:
                since = int(now - hr.get("face_present_since", now))
                if since > 300:
                    parts.append(f"Person present: {since // 60}min")
                elif since > 30:
                    parts.append(f"Person present: {since}s")
            elif hr.get("face_absent_since"):
                gone = int(now - hr["face_absent_since"])
                if gone > 600:
                    parts.append(f"Alone: {gone // 60}min")

            # Unresolved threads — factual
            unresolved = [t for t in self.open_threads
                         if not t["acknowledged"] and
                         t["times_mentioned"] >= 2 and
                         now - t["last_mentioned"] < 600]
            if unresolved:
                topics = ", ".join(t["topic"] for t in unresolved[:2])
                parts.append(f"Unacknowledged topics: {topics}")

            # Need levels as data
            if stimulation > 0.7:
                parts.append("Stimulation need: high (understimulated)")
            elif stimulation > 0.5:
                parts.append("Stimulation need: moderate")

            self.mood_narrative = " | ".join(parts)

    # ═══════════════════════════════════════════════════════
    # EVENT LOGGING
    # ═══════════════════════════════════════════════════════

    def record_event(self, event_type, buddy_reaction="noticed"):
        """Record a notable event with Buddy's reaction."""
        with self.lock:
            self.recent_events.append({
                "event": event_type,
                "time": time.time(),
                "buddy_reaction": buddy_reaction,
            })

    # ═══════════════════════════════════════════════════════
    # CONTEXT BUILDER — The main output of this engine
    # ═══════════════════════════════════════════════════════

    def get_narrative_context(self):
        """
        Build the full narrative context string for LLM consumption.
        This is what makes Buddy feel like he has continuous memory.
        """
        with self.lock:
            now = time.time()
            sections = []

            # 1. Recent utterance history
            if self.utterance_history:
                utterance_lines = []
                for entry in self.utterance_history:
                    age = int(now - entry["time"])
                    if age < 60:
                        time_str = f"{age}s ago"
                    elif age < 3600:
                        time_str = f"{age // 60}min ago"
                    else:
                        time_str = f"{age // 3600}h ago"

                    response_str = entry["response"]
                    if entry["response_type"] and entry["response_type"] != "none":
                        response_str = entry["response_type"]

                    text_short = entry["text"][:80]
                    if len(entry["text"]) > 80:
                        text_short += "..."

                    utterance_lines.append(
                        f'  - "{text_short}" ({time_str}, response: {response_str})'
                    )

                sections.append(
                    "What you've said recently:\n" + "\n".join(utterance_lines)
                )

            # 2. Open threads
            active_threads = [
                t for t in self.open_threads
                if not t["acknowledged"] and now - t["last_mentioned"] < 900
            ]
            if active_threads:
                thread_lines = []
                for t in active_threads:
                    age = int(now - t["first_mentioned"])
                    if age < 60:
                        age_str = f"{age}s"
                    else:
                        age_str = f"{age // 60}min"
                    thread_lines.append(
                        f"  - {t['topic']} (mentioned {t['times_mentioned']}x over {age_str}, "
                        f"{'acknowledged' if t['acknowledged'] else 'no response'})"
                    )
                sections.append(
                    "Topics you've brought up:\n" + "\n".join(thread_lines)
                )

            # 3. Human responsiveness
            hr = self.human_responsiveness
            responsiveness_parts = []

            if hr["face_present"]:
                since = int(now - hr.get("face_present_since", now))
                if since > 60:
                    responsiveness_parts.append(
                        f"Person has been here {since // 60} minutes."
                    )
                else:
                    responsiveness_parts.append(
                        f"Person arrived {since} seconds ago."
                    )
            else:
                if hr.get("face_absent_since") and hr["face_absent_since"] > 0:
                    gone = int(now - hr["face_absent_since"])
                    if gone > 60:
                        responsiveness_parts.append(
                            f"You've been alone for {gone // 60} minutes."
                        )
                    else:
                        responsiveness_parts.append(
                            f"Person left {gone} seconds ago."
                        )
                else:
                    responsiveness_parts.append("Nobody is around.")

            responsiveness_parts.append(
                f"Their attention level: {hr['attention_level']}. "
                f"Pattern: {hr['pattern']}."
            )

            if hr["ignored_streak"] > 0:
                responsiveness_parts.append(
                    f"You've been ignored {hr['ignored_streak']} times in a row."
                )

            sections.append(
                "Human responsiveness:\n  " + " ".join(responsiveness_parts)
            )

            # 4. Mood narrative
            if self.mood_narrative:
                sections.append(f"Your internal state: {self.mood_narrative}")

            # 5. Object memory
            obj_ctx = self._get_object_context_unlocked()
            if obj_ctx:
                sections.append(obj_ctx)

            # 6. Recent events
            recent = [
                e for e in self.recent_events
                if now - e["time"] < 300
            ]
            if recent:
                event_lines = []
                for e in recent[-5:]:
                    age = int(now - e["time"])
                    if age < 60:
                        t_str = f"{age}s ago"
                    else:
                        t_str = f"{age // 60}min ago"
                    event_lines.append(
                        f"  - {e['event'].replace('_', ' ')} ({t_str})"
                    )
                sections.append(
                    "Recent events:\n" + "\n".join(event_lines)
                )

            # 7. Person profile context
            person_ctx = self._get_person_context_unlocked()
            if person_ctx:
                sections.append(f"What you know about this person:\n  {person_ctx}")

            # 8. Session context
            session_min = int((now - self.session_start) / 60)
            if session_min > 0:
                sections.append(
                    f"Session: {session_min} minutes, "
                    f"{self.total_utterances_session} things said."
                )

            return "\n\n".join(sections)

    # ═══════════════════════════════════════════════════════
    # OBJECT MEMORY — Persistent awareness of things in the scene
    # ═══════════════════════════════════════════════════════

    TRACKABLE_OBJECTS = {
        "mug": ["mug", "cup"],
        "coffee": ["coffee"],
        "phone": ["phone", "cell", "mobile"],
        "laptop": ["laptop"],
        "monitor": ["monitor", "screen", "display"],
        "keyboard": ["keyboard"],
        "mouse": ["mouse"],
        "book": ["book"],
        "pen": ["pen", "pencil"],
        "bottle": ["bottle", "water bottle"],
        "plate": ["plate"],
        "headphones": ["headphones", "earbuds"],
        "plant": ["plant"],
        "lamp": ["lamp"],
        "clock": ["clock"],
        "glasses": ["glasses"],
        "cat": ["cat"],
        "dog": ["dog"],
    }

    def update_object_memory(self, scene_description):
        """
        Scan scene description for objects and update persistent memory.
        Detects new objects, disappeared objects, and state changes.

        Returns: list of events like "mug_appeared", "phone_disappeared"
        """
        with self.lock:
            now = time.time()
            desc_lower = scene_description.lower() if scene_description else ""
            events = []
            currently_seen = set()

            for obj_name, keywords in self.TRACKABLE_OBJECTS.items():
                if any(kw in desc_lower for kw in keywords):
                    currently_seen.add(obj_name)

                    if obj_name not in self.object_memory:
                        # New object spotted!
                        self.object_memory[obj_name] = {
                            "first_seen": now,
                            "last_seen": now,
                            "times_seen": 1,
                            "mentioned_by_buddy": False,
                            "disappeared_at": None,
                        }
                        events.append(f"{obj_name}_appeared")
                    else:
                        mem = self.object_memory[obj_name]
                        mem["last_seen"] = now
                        mem["times_seen"] += 1
                        # Object returned after disappearing
                        if mem.get("disappeared_at"):
                            gone_for = int(now - mem["disappeared_at"])
                            events.append(f"{obj_name}_returned_after_{gone_for}s")
                            mem["disappeared_at"] = None

            # Check for objects that disappeared
            for obj_name, mem in self.object_memory.items():
                if (obj_name not in currently_seen and
                        not mem.get("disappeared_at") and
                        now - mem["last_seen"] > 20):  # 20s grace period
                    mem["disappeared_at"] = now
                    events.append(f"{obj_name}_disappeared")

            return events

    def get_object_context(self):
        """Build object memory context for LLM consumption.
        Note: Call this without the lock held, OR from within a locked context
        (the internal _get_object_context_unlocked handles the actual work)."""
        with self.lock:
            return self._get_object_context_unlocked()

    def _get_object_context_unlocked(self):
        """Build object context (caller must hold self.lock)."""
        now = time.time()
        if not self.object_memory:
            return ""

        parts = []
        for obj_name, mem in self.object_memory.items():
            age = int(now - mem["first_seen"])
            if age < 60:
                age_str = f"{age}s"
            else:
                age_str = f"{age // 60}min"

            if mem.get("disappeared_at"):
                gone_for = int(now - mem["disappeared_at"])
                if gone_for < 300:
                    parts.append(
                        f"  - {obj_name}: was here, disappeared {gone_for}s ago"
                    )
            elif mem["times_seen"] > 3 and not mem["mentioned_by_buddy"]:
                parts.append(
                    f"  - {obj_name}: been there for {age_str}, "
                    f"you haven't mentioned it yet"
                )
            elif mem["mentioned_by_buddy"]:
                parts.append(
                    f"  - {obj_name}: noticed {age_str} ago (you mentioned it)"
                )

        if not parts:
            return ""
        return "Objects you're aware of:\n" + "\n".join(parts)

    def mark_object_mentioned(self, text):
        """Mark objects as mentioned by Buddy based on his speech text."""
        with self.lock:
            text_lower = text.lower()
            for obj_name, keywords in self.TRACKABLE_OBJECTS.items():
                if obj_name in self.object_memory:
                    if any(kw in text_lower for kw in keywords):
                        self.object_memory[obj_name]["mentioned_by_buddy"] = True

    # ═══════════════════════════════════════════════════════
    # PERSON PROFILES — Per-person behavioral memory
    # ═══════════════════════════════════════════════════════

    def set_current_person(self, person_id):
        """Track which person Buddy is interacting with."""
        with self.lock:
            self._current_person_id = person_id
            if person_id and person_id not in self.person_profiles:
                self.person_profiles[person_id] = {
                    "first_seen": time.time(),
                    "interaction_count": 0,
                    "total_responses": 0,
                    "total_ignores": 0,
                    "avg_response_delay": 0.0,
                    "preferred_timing": "normal",
                    "responded_to_strategies": defaultdict(int),
                    "ignored_strategies": defaultdict(int),
                    "last_interaction": time.time(),
                }

    def record_person_response(self, response_type, strategy=None, delay=0.0):
        """Record a person's response to Buddy's speech."""
        with self.lock:
            pid = self._current_person_id
            if not pid or pid not in self.person_profiles:
                return
            p = self.person_profiles[pid]
            p["interaction_count"] += 1
            p["last_interaction"] = time.time()

            if response_type in ("smiled", "laughed", "spoke", "looked"):
                p["total_responses"] += 1
                if strategy:
                    p["responded_to_strategies"][strategy] += 1
                # Update average response delay
                if delay > 0:
                    n = p["total_responses"]
                    p["avg_response_delay"] = (
                        (p["avg_response_delay"] * (n - 1) + delay) / n
                    )
                    if p["avg_response_delay"] < 3:
                        p["preferred_timing"] = "quick"
                    elif p["avg_response_delay"] > 8:
                        p["preferred_timing"] = "slow"
                    else:
                        p["preferred_timing"] = "normal"
            else:
                p["total_ignores"] += 1
                if strategy:
                    p["ignored_strategies"][strategy] += 1

    def _get_person_context_unlocked(self):
        """Build person context (caller must hold self.lock)."""
        pid = self._current_person_id
        if not pid or pid not in self.person_profiles:
            return ""
        p = self.person_profiles[pid]
        if p["interaction_count"] < 3:
            return ""

        parts = []
        total = p["total_responses"] + p["total_ignores"]
        if total > 0:
            rate = p["total_responses"] / total
            if rate > 0.7:
                parts.append("This person is generally responsive to you.")
            elif rate < 0.3:
                parts.append("This person usually ignores you.")

        if p["responded_to_strategies"]:
            best = max(
                p["responded_to_strategies"],
                key=p["responded_to_strategies"].get
            )
            parts.append(
                f"They respond best to: {best.replace('_', ' ')}"
            )

        if p["preferred_timing"] == "quick":
            parts.append("They react quickly — be responsive.")
        elif p["preferred_timing"] == "slow":
            parts.append("They take time to react — be patient.")

        return " ".join(parts) if parts else ""

    def get_person_context(self):
        """Build person-specific context for the LLM."""
        with self.lock:
            pid = self._current_person_id
            if not pid or pid not in self.person_profiles:
                return ""
            p = self.person_profiles[pid]
            if p["interaction_count"] < 3:
                return ""  # Not enough data yet

            parts = []
            total = p["total_responses"] + p["total_ignores"]
            if total > 0:
                rate = p["total_responses"] / total
                if rate > 0.7:
                    parts.append("This person is generally responsive to you.")
                elif rate < 0.3:
                    parts.append("This person usually ignores you.")

            # What strategies worked
            if p["responded_to_strategies"]:
                best = max(
                    p["responded_to_strategies"],
                    key=p["responded_to_strategies"].get
                )
                parts.append(
                    f"They respond best to: {best.replace('_', ' ')}"
                )

            if p["preferred_timing"] == "quick":
                parts.append("They react quickly — be responsive.")
            elif p["preferred_timing"] == "slow":
                parts.append("They take time to react — be patient.")

            return "\n".join(parts) if parts else ""

    def get_person_profiles_data(self):
        """Return serializable person profiles for persistence."""
        with self.lock:
            result = {}
            for pid, p in self.person_profiles.items():
                result[pid] = {
                    "first_seen": p["first_seen"],
                    "interaction_count": p["interaction_count"],
                    "total_responses": p["total_responses"],
                    "total_ignores": p["total_ignores"],
                    "avg_response_delay": p["avg_response_delay"],
                    "preferred_timing": p["preferred_timing"],
                    "responded_to_strategies": dict(p["responded_to_strategies"]),
                    "ignored_strategies": dict(p["ignored_strategies"]),
                    "last_interaction": p["last_interaction"],
                }
            return result

    def load_person_profiles(self, data):
        """Load person profiles from persistence."""
        with self.lock:
            for pid, p in data.items():
                profile = {
                    "first_seen": p.get("first_seen", 0),
                    "interaction_count": p.get("interaction_count", 0),
                    "total_responses": p.get("total_responses", 0),
                    "total_ignores": p.get("total_ignores", 0),
                    "avg_response_delay": p.get("avg_response_delay", 0.0),
                    "preferred_timing": p.get("preferred_timing", "normal"),
                    "responded_to_strategies": defaultdict(
                        int, p.get("responded_to_strategies", {})
                    ),
                    "ignored_strategies": defaultdict(
                        int, p.get("ignored_strategies", {})
                    ),
                    "last_interaction": p.get("last_interaction", 0),
                }
                self.person_profiles[pid] = profile

    # ═══════════════════════════════════════════════════════
    # TIME-SINCE HELPERS
    # ═══════════════════════════════════════════════════════

    def time_since_last_speech(self):
        """Seconds since Buddy last spoke."""
        if self.last_speech_time == 0:
            return float('inf')
        return time.time() - self.last_speech_time

    def time_since_last_interaction(self):
        """Seconds since last human interaction."""
        last = self.human_responsiveness["last_interaction"]
        if last == 0:
            return float('inf')
        return time.time() - last

    def get_ignored_streak(self):
        """How many consecutive utterances were ignored."""
        return self.human_responsiveness["ignored_streak"]

    def get_attention_level(self):
        """Current attention level: absent / low / medium / high / unknown."""
        return self.human_responsiveness["attention_level"]

    def get_pattern(self):
        """Current interaction pattern."""
        return self.human_responsiveness["pattern"]

    def is_person_present(self):
        """Whether someone is currently visible."""
        return self.human_responsiveness["face_present"]

    # ═══════════════════════════════════════════════════════
    # CROSS-SESSION PERSISTENCE
    # ═══════════════════════════════════════════════════════

    MEMORY_FILE = os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "buddy_memory.json"
    )

    def save_memory(self, strategy_tracker=None):
        """Save persistent state to disk. Call on shutdown or periodically."""
        with self.lock:
            data = {
                "version": 1,
                "saved_at": time.time(),
                "person_profiles": {},
                "object_familiarity": {},
                "strategy_stats": {},
                "session_count": 0,
            }

            # Person profiles
            for pid, p in self.person_profiles.items():
                data["person_profiles"][pid] = {
                    "first_seen": p.get("first_seen", 0),
                    "interaction_count": p.get("interaction_count", 0),
                    "total_responses": p.get("total_responses", 0),
                    "total_ignores": p.get("total_ignores", 0),
                    "avg_response_delay": p.get("avg_response_delay", 0.0),
                    "preferred_timing": p.get("preferred_timing", "normal"),
                    "responded_to_strategies": dict(
                        p.get("responded_to_strategies", {})
                    ),
                    "ignored_strategies": dict(
                        p.get("ignored_strategies", {})
                    ),
                    "last_interaction": p.get("last_interaction", 0),
                }

            # Object familiarity (long-term: how familiar is each object)
            for name, mem in self.object_memory.items():
                data["object_familiarity"][name] = {
                    "first_seen": mem.get("first_seen", 0),
                    "times_seen": mem.get("times_seen", 0),
                    "mentioned_by_buddy": mem.get("mentioned_by_buddy", False),
                }

        # Strategy stats (outside narrative lock to avoid nesting)
        if strategy_tracker:
            data["strategy_stats"] = strategy_tracker.get_stats_summary()

        # Load existing to increment session count
        try:
            with open(self.MEMORY_FILE, "r") as f:
                existing = json.load(f)
                data["session_count"] = existing.get("session_count", 0) + 1
        except (FileNotFoundError, json.JSONDecodeError):
            data["session_count"] = 1

        try:
            tmp_path = self.MEMORY_FILE + ".tmp"
            with open(tmp_path, "w") as f:
                json.dump(data, f, indent=2)
            os.replace(tmp_path, self.MEMORY_FILE)
            print(f"[MEMORY] Saved to {self.MEMORY_FILE} "
                  f"({len(data['person_profiles'])} profiles, "
                  f"session #{data['session_count']})")
        except Exception as e:
            print(f"[MEMORY] Save failed: {e}")

    def load_memory(self, strategy_tracker=None):
        """Load persistent state from disk. Call on startup."""
        try:
            with open(self.MEMORY_FILE, "r") as f:
                data = json.load(f)
        except (FileNotFoundError, json.JSONDecodeError):
            print("[MEMORY] No saved memory found — starting fresh")
            return

        version = data.get("version", 0)
        if version < 1:
            print("[MEMORY] Incompatible memory version, skipping")
            return

        # Load person profiles
        profiles = data.get("person_profiles", {})
        if profiles:
            self.load_person_profiles(profiles)
            print(f"[MEMORY] Loaded {len(profiles)} person profiles")

        # Load object familiarity into object_memory
        with self.lock:
            for name, fam in data.get("object_familiarity", {}).items():
                if name not in self.object_memory:
                    self.object_memory[name] = {
                        "first_seen": fam.get("first_seen", 0),
                        "last_seen": fam.get("first_seen", 0),
                        "times_seen": fam.get("times_seen", 0),
                        "mentioned_by_buddy": fam.get(
                            "mentioned_by_buddy", False
                        ),
                        "disappeared_at": None,
                    }

        # Load strategy stats
        if strategy_tracker and "strategy_stats" in data:
            strategy_tracker.load_stats(data["strategy_stats"])
            print("[MEMORY] Loaded strategy success rates")

        session = data.get("session_count", 0)
        saved_at = data.get("saved_at", 0)
        if saved_at > 0:
            age_hours = (time.time() - saved_at) / 3600
            print(f"[MEMORY] Session #{session + 1}, "
                  f"last save {age_hours:.1f}h ago")
