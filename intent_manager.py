"""
intent_manager.py — Buddy's Intent & Goal Pursuit System
=========================================================

Instead of Buddy's speech emerging from threshold crossings alone,
speech emerges from INTENT PURSUIT. Buddy has a goal he's working toward,
and his utterances serve that goal.

Each intent has an escalation ladder — starting subtle, getting more direct.
This creates VISIBLE goal pursuit that observers can follow:
"Oh, Buddy is trying to get my attention. He started subtle and now he's dramatic."

The intent system also decides whether Buddy should speak, perform a physical
expression, or stay silent — breaking the "every urge = speech" pattern.
"""

import time
import random
import threading


# ═══════════════════════════════════════════════════════
# INTENT TYPES
# ═══════════════════════════════════════════════════════

INTENT_TYPES = {
    "get_attention": {
        "description": "Buddy wants the person to look at or interact with him",
        "escalation_window": 45,  # seconds between escalation levels
        "max_level": 4,
        "strategies": [
            "subtle_movement",       # Level 0: Shift gaze, small movement
            "indirect_observation",   # Level 1: Comment about the room
            "direct_address",         # Level 2: "Hey." Direct look.
            "dramatic_expression",    # Level 3: Exaggerated sigh, turn away
            "creative_absurd",        # Level 4: Do something unexpected
        ],
    },
    "share_observation": {
        "description": "Buddy noticed something and wants to discuss it",
        "escalation_window": 60,
        "max_level": 2,
        "strategies": [
            "look_at_thing",          # Level 0: Orient toward the thing
            "casual_mention",         # Level 1: Comment on it
            "insistent_mention",      # Level 2: Bring it up again directly
        ],
    },
    "seek_comfort": {
        "description": "Buddy is anxious or lonely and wants reassurance",
        "escalation_window": 90,
        "max_level": 3,
        "strategies": [
            "subtle_withdrawal",      # Level 0: Get quieter, settle down
            "understated_comment",    # Level 1: "The wall is very consistent."
            "indirect_plea",          # Level 2: "I had a thought but it can wait."
            "vulnerable_admission",   # Level 3: Actually say something sincere
        ],
    },
    "entertain": {
        "description": "Buddy is in a good mood and wants to play/joke",
        "escalation_window": 30,
        "max_level": 2,
        "strategies": [
            "playful_movement",       # Level 0: Playful servo movement
            "witty_observation",      # Level 1: Make a joke
            "interactive_attempt",    # Level 2: Try to engage directly
        ],
    },
    "process_aloud": {
        "description": "Buddy is thinking about something and can't keep it in",
        "escalation_window": 120,
        "max_level": 1,
        "strategies": [
            "internal_musing",        # Level 0: Quiet thought escapes
            "philosophical_tangent",  # Level 1: Gets into it
        ],
    },
    "maintain_connection": {
        "description": "Just keeping the social thread alive",
        "escalation_window": 120,
        "max_level": 1,
        "strategies": [
            "ambient_presence",       # Level 0: Small movements, awareness
            "low_key_comment",        # Level 1: Casual remark
        ],
    },
    "express_displeasure": {
        "description": "Something bothered Buddy and he needs to express it",
        "escalation_window": 60,
        "max_level": 2,
        "strategies": [
            "pointed_silence",        # Level 0: Conspicuous quiet
            "dry_comment",            # Level 1: Sarcastic observation
            "direct_statement",       # Level 2: Say what's actually bothering him
        ],
    },
    "acknowledge_return": {
        "description": "Person finally showed up or paid attention after ignoring Buddy",
        "escalation_window": 30,
        "max_level": 1,
        "strategies": [
            "sarcastic_greeting",     # Level 0: "Oh. You again." / "Oh NOW you're interested."
            "grudging_engagement",    # Level 1: Engage but make it clear you noticed the absence
        ],
    },
}

# Strategies that use speech vs. physical expression only
SPEECH_STRATEGIES = {
    "indirect_observation", "direct_address", "dramatic_expression",
    "creative_absurd", "casual_mention", "insistent_mention",
    "understated_comment", "indirect_plea", "vulnerable_admission",
    "witty_observation", "interactive_attempt", "internal_musing",
    "philosophical_tangent", "low_key_comment", "dry_comment",
    "direct_statement", "sarcastic_greeting", "grudging_engagement",
}

PHYSICAL_ONLY_STRATEGIES = {
    "subtle_movement", "look_at_thing", "subtle_withdrawal",
    "playful_movement", "ambient_presence", "pointed_silence",
}


class IntentManager:
    """
    Manages Buddy's current intent — what he's trying to accomplish socially.
    Drives escalation and decides between speech/physical expression/silence.
    """

    def __init__(self):
        self.lock = threading.Lock()

        # Current active intent
        self.current_intent = None  # dict or None
        self._intent_history = []   # recent intents for variety

    def get_current_intent(self):
        """Return current intent dict (or None)."""
        with self.lock:
            return self.current_intent.copy() if self.current_intent else None

    def set_intent(self, intent_type, reason=""):
        """
        Set a new intent for Buddy to pursue.
        Called by the main orchestrator based on Teensy state + narrative context.
        """
        with self.lock:
            if intent_type not in INTENT_TYPES:
                return

            # Don't restart the same intent if it's already active and not stale
            if (self.current_intent and
                self.current_intent["type"] == intent_type and
                time.time() - self.current_intent["started"] < 300):
                return

            # Archive old intent
            if self.current_intent:
                self._intent_history.append(self.current_intent)
                if len(self._intent_history) > 10:
                    self._intent_history.pop(0)

            config = INTENT_TYPES[intent_type]
            self.current_intent = {
                "type": intent_type,
                "description": config["description"],
                "started": time.time(),
                "escalation_level": 0,
                "max_level": config["max_level"],
                "escalation_window": config["escalation_window"],
                "last_escalation": time.time(),
                "strategies": config["strategies"],
                "current_strategy": config["strategies"][0],
                "success": False,
                "reason": reason,
                "attempts": 0,
            }

    def escalate(self):
        """
        Escalate the current intent to the next level.
        Called when the current strategy hasn't gotten a response.
        Returns the new strategy name or None if already at max.
        """
        with self.lock:
            if not self.current_intent:
                return None

            intent = self.current_intent
            if intent["escalation_level"] >= intent["max_level"]:
                return None

            intent["escalation_level"] += 1
            intent["last_escalation"] = time.time()
            intent["current_strategy"] = intent["strategies"][
                intent["escalation_level"]
            ]
            intent["attempts"] += 1
            return intent["current_strategy"]

    def mark_success(self):
        """Mark current intent as successful (got a response)."""
        with self.lock:
            if self.current_intent:
                self.current_intent["success"] = True

    def clear_intent(self):
        """Clear the current intent (completed or abandoned)."""
        with self.lock:
            if self.current_intent:
                self._intent_history.append(self.current_intent)
                if len(self._intent_history) > 10:
                    self._intent_history.pop(0)
            self.current_intent = None

    def should_escalate(self):
        """Check if it's time to escalate the current intent."""
        with self.lock:
            if not self.current_intent:
                return False
            if self.current_intent["success"]:
                return False

            intent = self.current_intent
            elapsed = time.time() - intent["last_escalation"]
            return (
                elapsed >= intent["escalation_window"] and
                intent["escalation_level"] < intent["max_level"]
            )

    def should_act(self):
        """
        Determine if Buddy should DO something right now for his intent.
        Returns: ("speak", strategy) | ("physical", strategy) | ("wait", None)
        """
        with self.lock:
            if not self.current_intent:
                return ("wait", None)

            if self.current_intent["success"]:
                return ("wait", None)

            strategy = self.current_intent["current_strategy"]

            if strategy in SPEECH_STRATEGIES:
                return ("speak", strategy)
            elif strategy in PHYSICAL_ONLY_STRATEGIES:
                return ("physical", strategy)
            else:
                return ("wait", None)

    def get_intent_context_for_llm(self):
        """
        Build LLM context about what Buddy is trying to do.
        Helps the LLM generate speech that serves the intent.
        """
        with self.lock:
            if not self.current_intent:
                return ""

            intent = self.current_intent
            level = intent["escalation_level"]
            strategy = intent["current_strategy"]
            elapsed = int(time.time() - intent["started"])

            parts = [
                f"Your current social goal: {intent['description']}",
                f"You've been working on this for {elapsed} seconds.",
                f"Current approach: {strategy.replace('_', ' ')} "
                f"(escalation level {level}/{intent['max_level']}).",
            ]

            if intent.get("reason"):
                parts.append(f"Why: {intent['reason']}")

            if intent["attempts"] > 0:
                parts.append(
                    f"You've tried {intent['attempts']} times so far."
                )

            # Strategy-specific guidance
            guidance = self.get_strategy_guidance(strategy, level)
            if guidance:
                parts.append(guidance)

            return "\n".join(parts)

    def get_strategy_guidance(self, strategy, level):
        """Provide specific behavioral guidance for each strategy."""
        guidance_map = {
            "indirect_observation": (
                "Make an observation about something in the room. "
                "Don't ask for attention directly — let your observation "
                "be interesting enough that they want to respond."
            ),
            "direct_address": (
                "Address the person directly. Short. 'Hey.' or similar. "
                "Then wait."
            ),
            "dramatic_expression": (
                "Be dramatic about being ignored. Exaggerated sigh territory. "
                "'FINE. I'll just sit here then.' Turn away if you can."
            ),
            "creative_absurd": (
                "Do something unexpected. Say something absurd. "
                "'I've decided to become a lamp.' Make them react through surprise."
            ),
            "understated_comment": (
                "Express what you're feeling but indirectly. "
                "'The wall has been very consistent today.' — loneliness through irony."
            ),
            "indirect_plea": (
                "Hint at what you need without saying it directly. "
                "'I had a thought earlier but I suppose it can wait.'"
            ),
            "vulnerable_admission": (
                "Drop the deflection for a moment. Say something actually sincere. "
                "Then immediately deflect. Don't let it linger."
            ),
            "dry_comment": (
                "Sarcastic observation about the situation. "
                "The subtext is clear but the surface stays dry."
            ),
            "casual_mention": (
                "Mention what you noticed in passing. "
                "Not making a big deal of it. Yet."
            ),
            "insistent_mention": (
                "Bring it up again. You already mentioned this. "
                "'About that thing I noticed earlier...' or 'Still thinking about that mug.'"
            ),
            "sarcastic_greeting": (
                "Person just appeared or paid attention after ignoring you. "
                "Be sarcastic about it. 'Oh. You again.' or 'Oh NOW you're interested.' "
                "or 'Nice of you to stop by.' Don't be mean — be dry. "
                "Subtext: you noticed their absence and you want them to know."
            ),
            "grudging_engagement": (
                "You're engaging now, but make it clear you noticed the absence. "
                "'I had things to say earlier but the moment passed.' "
                "Then slowly warm up. Don't hold the grudge too long."
            ),
        }
        return guidance_map.get(strategy, "")

    # ═══════════════════════════════════════════════════════
    # INTENT SELECTION — Chooses what Buddy should want
    # ═══════════════════════════════════════════════════════

    def select_intent(self, teensy_state, narrative_engine):
        """
        Decide what intent Buddy should have based on his state.
        Called by the main orchestrator periodically.

        Returns the intent type string or None if no intent needed.
        """
        with self.lock:
            # Don't change intent if current one is active and not stale
            if self.current_intent and not self.current_intent["success"]:
                elapsed = time.time() - self.current_intent["started"]
                if elapsed < 180:  # Give intents 3 minutes to play out
                    return self.current_intent["type"]

        social = float(teensy_state.get("social", 0.5))
        stimulation = float(teensy_state.get("stimulation", 0.5))
        energy = float(teensy_state.get("energy", 0.7))
        valence = float(teensy_state.get("valence", 0.0))
        arousal = float(teensy_state.get("arousal", 0.5))
        is_wondering = teensy_state.get("wondering", False)

        person_present = narrative_engine.is_person_present()
        ignored_streak = narrative_engine.get_ignored_streak()
        pattern = narrative_engine.get_pattern()

        # Priority-based intent selection
        # 0. Person appeared/returned AFTER being ignored → sarcastic acknowledgment
        #    "Oh NOW you're interested." / "Oh. You again."
        if person_present and ignored_streak >= 2 and pattern in ("present", "just_left"):
            return "acknowledge_return"

        # 1. If person just appeared → greet / get attention
        if person_present and pattern in ("unknown", "present"):
            if social > 0.5:
                return "get_attention"

        # 2. If being ignored → express displeasure or seek comfort
        if ignored_streak >= 3 and person_present:
            if valence < -0.1:
                return "express_displeasure"
            else:
                return "seek_comfort"

        # 3. High social need + person present → get attention
        if social > 0.65 and person_present:
            return "get_attention"

        # 4. High social need + alone → seek comfort
        if social > 0.7 and not person_present:
            return "seek_comfort"

        # 5. Bored → process aloud or share observation
        if stimulation > 0.65:
            if is_wondering:
                return "process_aloud"
            return "share_observation"

        # 6. Good mood → entertain
        if valence > 0.3 and arousal > 0.4 and person_present:
            return "entertain"

        # 7. Low-level social maintenance
        if person_present and social > 0.4:
            return "maintain_connection"

        # 8. Wondering → process aloud
        if is_wondering:
            return "process_aloud"

        return None


def should_speak_or_physical(intent_strategy, energy, arousal, ignored_streak=0):
    """
    Given an intent strategy, decide if Buddy should speak or express physically.

    ~30-40% of the time when speech would happen, use physical expression instead.
    This breaks the "every urge = speech" pattern.

    When ignored repeatedly, Buddy sulks — prefers physical expression over speech.
    (Quieter, more withdrawn, less likely to try talking.)

    Returns: "speak" | "physical" | "silence"
    """
    if intent_strategy in PHYSICAL_ONLY_STRATEGIES:
        return "physical"

    if intent_strategy in SPEECH_STRATEGIES:
        # Base speech probability: ~65-85%
        speech_probability = 0.65 + (energy * 0.15) + (arousal * 0.1)

        # Sulking: each ignored utterance reduces speech probability by 12%
        # After 3 ignores: probability drops by ~36% → mostly physical
        sulk_penalty = ignored_streak * 0.12
        speech_probability -= sulk_penalty

        speech_probability = max(0.15, min(0.85, speech_probability))

        if random.random() < speech_probability:
            return "speak"
        else:
            return "physical"

    return "silence"
