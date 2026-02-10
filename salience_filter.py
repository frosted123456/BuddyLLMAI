"""
salience_filter.py — Attention Salience Filter for Buddy
=========================================================

Scores scene descriptions BEFORE they reach Teensy to prevent Buddy
from caring about walls, shadows, and irrelevant details.

Also converts periodic vision updates to event-driven — only sends
emotion nudges when something ACTUALLY CHANGED.

SALIENCE LEVELS:
  5 — Face / person (always relevant)
  4 — New person, expression change, person left/returned
  3 — New object appeared, significant object in use (phone, cup)
  2 — Familiar object, room feature mentioned first time
  1 — Static environment (wall, floor, furniture mentioned again)
  0 — Suppress entirely (wall texture, shadows, ambiguous)
"""

import time
import threading
import re


class SalienceFilter:
    """
    Filters and scores scene descriptions to prevent attention scatter.
    Converts periodic updates into event-driven ones.
    """

    def __init__(self):
        self.lock = threading.Lock()

        # Last reported state — only send updates when delta is meaningful
        self._last_reported = {
            "face_present": None,
            "face_count": 0,
            "expression": "neutral",
            "scene_description": "",
            "notable_objects": set(),
            "last_update_time": 0,
        }

        # Expression stability tracking — don't report fleeting changes
        self._expression_buffer = []
        self._expression_stable_since = 0
        self._stable_expression = "neutral"

        # Scene description novelty
        self._description_history = []

    def score_description(self, description):
        """
        Score a scene description for salience (0-5).
        Higher scores = more interesting = should trigger attention.

        Returns: (score, reason)
        """
        if not description:
            return (0, "empty")

        desc_lower = description.lower()
        score = 0
        reasons = []

        # Level 5: Person/face directly mentioned
        person_words = [
            "person", "someone", "man", "woman", "people", "face",
            "they", "he", "she", "user", "sitting", "standing",
            "looking", "working", "typing"
        ]
        if any(w in desc_lower for w in person_words):
            score = max(score, 5)
            reasons.append("person_detected")

        # Level 4: Person state changes
        change_words = [
            "appeared", "returned", "left", "gone", "came back",
            "smiling", "frowning", "angry", "surprised", "laughing",
            "sad", "yawning", "moved closer", "moved away"
        ]
        if any(w in desc_lower for w in change_words):
            score = max(score, 4)
            reasons.append("person_change")

        # Level 3: Active objects (things being used or newly appearing)
        active_object_words = [
            "phone", "cup being", "coffee", "tea", "eating", "drinking",
            "headphones", "typing", "writing", "opened", "closed",
            "picked up", "put down", "new"
        ]
        if any(w in desc_lower for w in active_object_words):
            score = max(score, 3)
            reasons.append("active_object")

        # Level 3: New object appeared
        new_object_indicators = ["new", "appeared", "now there", "wasn't there"]
        if any(w in desc_lower for w in new_object_indicators):
            score = max(score, 3)
            reasons.append("new_item")

        # Level 2: Static objects (not being actively used)
        static_objects = [
            "mug", "cup", "book", "laptop", "monitor", "keyboard",
            "mouse", "pen", "paper", "bottle", "plate", "glass",
            "chair", "lamp", "plant", "clock"
        ]
        static_found = [w for w in static_objects if w in desc_lower]
        if static_found:
            # Only score 2 if these are being mentioned for the first time
            with self.lock:
                new_objects = set(static_found) - self._last_reported["notable_objects"]
            if new_objects:
                score = max(score, 2)
                reasons.append(f"new_objects: {', '.join(new_objects)}")
            else:
                score = max(score, 1)
                reasons.append("familiar_objects")

        # Level 1: Generic environment (repeated mentions)
        env_words = ["desk", "table", "room", "wall", "floor", "ceiling", "door"]
        if any(w in desc_lower for w in env_words) and score < 2:
            score = max(score, 1)
            reasons.append("environment")

        # Level 0: Suppress irrelevant
        suppress_words = [
            "shadow", "reflection", "texture", "light on wall",
            "corner of", "edge of", "similar to before",
            "unchanged", "same as", "nothing new"
        ]
        if any(w in desc_lower for w in suppress_words) and score < 2:
            score = 0
            reasons = ["suppressed_irrelevant"]

        return (score, ", ".join(reasons) if reasons else "generic")

    def should_send_vision_update(self, face_present, face_count, expression,
                                   scene_description, novelty):
        """
        Determine if a vision update should be sent to Teensy.
        Replaces the fixed 3-second timer with event-driven updates.

        Returns: (should_send, event_type, salience_score)
        """
        with self.lock:
            now = time.time()
            last = self._last_reported
            events = []

            # 1. Face presence change — ALWAYS send
            if face_present != last["face_present"]:
                if face_present:
                    events.append("person_appeared")
                else:
                    events.append("person_left")
                last["face_present"] = face_present

            # 2. Face count change
            if face_count != last["face_count"]:
                events.append("face_count_changed")
                last["face_count"] = face_count

            # 3. Expression change — only if stable for >1 second
            if expression != last["expression"]:
                self._expression_buffer.append((expression, now))
                # Check if new expression has been stable for >1s
                stable_expr = self._get_stable_expression()
                if stable_expr and stable_expr != last["expression"]:
                    events.append(f"expression_changed:{stable_expr}")
                    last["expression"] = stable_expr

            # 4. Scene description — score for salience
            score, reason = self.score_description(scene_description)
            if score >= 3 and scene_description != last["scene_description"]:
                events.append(f"scene_change:{reason}")
                last["scene_description"] = scene_description

                # Track notable objects
                desc_lower = scene_description.lower()
                objects = set()
                for obj in ["mug", "cup", "phone", "book", "laptop", "monitor",
                           "keyboard", "mouse", "pen", "bottle", "plate"]:
                    if obj in desc_lower:
                        objects.add(obj)
                last["notable_objects"].update(objects)

            # 5. High novelty
            if novelty > 0.5 and scene_description != last["scene_description"]:
                events.append("high_novelty")
                last["scene_description"] = scene_description

            # 6. Minimum heartbeat — at least every 30 seconds if something is happening
            time_since_last = now - last["last_update_time"]
            if not events and face_present and time_since_last > 30:
                events.append("heartbeat")
                score = max(score, 1)

            if events:
                last["last_update_time"] = now
                return (True, events[0], max(score, 3 if "person" in events[0] else score))

            return (False, None, 0)

    def _get_stable_expression(self):
        """
        Return the current expression only if it's been stable for >1 second.
        Prevents reporting fleeting expression changes.
        """
        now = time.time()
        # Clean old entries
        self._expression_buffer = [
            (expr, t) for expr, t in self._expression_buffer
            if now - t < 5
        ]

        if not self._expression_buffer:
            return None

        # Check if the most recent expression has been consistent for 1+ second
        latest_expr = self._expression_buffer[-1][0]
        consistent_since = None
        for expr, t in reversed(self._expression_buffer):
            if expr != latest_expr:
                break
            consistent_since = t

        if consistent_since and now - consistent_since >= 1.0:
            return latest_expr
        return None

    def get_filtered_context(self, scene_description, face_present, expression):
        """
        Return a filtered version of the scene description for LLM context.
        Items scoring >= 3: full context.
        Items scoring 1-2: available but not emphasized.
        Items scoring 0: dropped.
        """
        score, reason = self.score_description(scene_description)

        if score >= 3:
            return scene_description
        elif score >= 1:
            # Provide but de-emphasize
            return f"(background: {scene_description})"
        else:
            return ""
