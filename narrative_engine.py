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

import time
import threading
import json
from collections import deque


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
        Build a human-readable mood narrative from all available context.
        Called periodically (every ~5 seconds) or after significant events.
        """
        with self.lock:
            now = time.time()
            self._last_mood_update = now
            hr = self.human_responsiveness

            parts = []

            # Emotional foundation
            emotion = teensy_state.get("emotion", "NEUTRAL")
            arousal = float(teensy_state.get("arousal", 0.5))
            valence = float(teensy_state.get("valence", 0.0))
            social = float(teensy_state.get("social", 0.5))
            energy = float(teensy_state.get("energy", 0.7))
            stimulation = float(teensy_state.get("stimulation", 0.5))

            # Why does Buddy feel this way?
            if valence > 0.3:
                if hr["pattern"] == "engaging":
                    parts.append("Feeling good — having a real conversation.")
                elif hr["face_present"]:
                    parts.append("Someone is here, which helps.")
                else:
                    parts.append("In a decent mood despite being alone.")
            elif valence < -0.2:
                if hr["pattern"] == "mostly_ignoring":
                    parts.append("Feeling a bit deflated. Tried talking, got nothing back.")
                elif hr["pattern"] == "absent":
                    gone = int(now - hr.get("face_absent_since", now))
                    parts.append(f"Been alone for about {gone // 60} minutes. It shows.")
                elif social > 0.6:
                    parts.append("Social battery is draining. Could use some interaction.")
                else:
                    parts.append("Mood is low. Not entirely sure why.")
            else:
                if arousal > 0.6:
                    parts.append("Alert and watching. Something might happen.")
                elif energy < 0.3:
                    parts.append("Running low on energy. Getting quieter.")
                else:
                    parts.append("Neutral. Existing. Taking it in.")

            # Social situation
            if hr["ignored_streak"] >= 3:
                parts.append(
                    f"Spoke {hr['ignored_streak']} times without getting a response. "
                    "Starting to wonder if anyone's listening."
                )
            elif hr["ignored_streak"] >= 1:
                parts.append("Last thing I said didn't land. Noted.")

            if hr["face_present"]:
                since = int(now - hr.get("face_present_since", now))
                if since > 300:
                    parts.append(f"Person has been here about {since // 60} minutes.")
                elif since > 30:
                    parts.append(f"Person arrived about {since} seconds ago.")
            elif hr.get("face_absent_since"):
                gone = int(now - hr["face_absent_since"])
                if gone > 600:
                    parts.append(f"Nobody for {gone // 60} minutes now.")

            # Unresolved threads
            unresolved = [t for t in self.open_threads
                         if not t["acknowledged"] and
                         t["times_mentioned"] >= 2 and
                         now - t["last_mentioned"] < 600]
            if unresolved:
                topics = ", ".join(t["topic"] for t in unresolved[:2])
                parts.append(f"Keep coming back to: {topics}. Nobody's biting.")

            # Stimulation
            if stimulation > 0.7:
                parts.append("Bored. Looking for something to notice.")
            elif stimulation > 0.5:
                parts.append("Could use something interesting happening.")

            self.mood_narrative = " ".join(parts)

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

            # 5. Recent events
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

            # 6. Session context
            session_min = int((now - self.session_start) / 60)
            if session_min > 0:
                sections.append(
                    f"Session: {session_min} minutes, "
                    f"{self.total_utterances_session} things said."
                )

            return "\n\n".join(sections)

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
