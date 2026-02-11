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
            "sarcastic_greeting",     # Level 0: Dry acknowledgment of their return
            "grudging_engagement",    # Level 1: Engage but signal you noticed the absence
        ],
    },
    "disengage": {
        "description": "Buddy is giving up on getting attention — theatrical resignation",
        "escalation_window": 25,
        "max_level": 1,
        "strategies": [
            "theatrical_resignation",   # Level 0: Dramatic "fine, whatever" moment
            "pointed_disinterest",      # Level 1: Show of being unbothered
        ],
    },
    "self_occupy": {
        "description": "Buddy is doing his own thing, conspicuously ignoring the person",
        "escalation_window": 50,
        "max_level": 2,
        "strategies": [
            "idle_fidgeting",           # Level 0: Physical only — restless looking around
            "musing_to_self",           # Level 1: Thinking out loud, not directed at person
            "passive_commentary",       # Level 2: Observations with subtext
        ],
    },
    "reluctant_reengage": {
        "description": "Buddy cautiously tries again after giving up — guarded, skeptical",
        "escalation_window": 50,
        "max_level": 1,
        "strategies": [
            "skeptical_approach",       # Level 0: Testing the waters
            "cautious_engagement",      # Level 1: Engaging but with residual attitude
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
    # Engagement cycle strategies
    "theatrical_resignation", "pointed_disinterest",
    "musing_to_self", "passive_commentary",
    "skeptical_approach", "cautious_engagement",
}

PHYSICAL_ONLY_STRATEGIES = {
    "subtle_movement", "look_at_thing", "subtle_withdrawal",
    "playful_movement", "ambient_presence", "pointed_silence",
    # Engagement cycle
    "idle_fidgeting",
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

        # ── Engagement cycle ──
        # Tracks Buddy's arc: eager → persistent → giving up → self-occupied → reluctant retry
        self._engagement_phase = "idle"    # idle / engaging / giving_up / self_occupied
        self._gave_up_at = 0               # timestamp of last give-up
        self._gave_up_count = 0            # times given up this person-visit
        self._cooldown_until = 0           # don't re-engage until this time

    def get_current_intent(self):
        """Return current intent dict (or None)."""
        with self.lock:
            return self.current_intent.copy() if self.current_intent else None

    def get_engagement_phase(self):
        """Return the current engagement cycle phase."""
        with self.lock:
            return self._engagement_phase

    def person_departed(self):
        """Called when face tracking loses the person. Resets engagement cycle."""
        with self.lock:
            if self._engagement_phase in ("engaging", "giving_up"):
                self._engagement_phase = "idle"
            # Keep self_occupied — Buddy still sulking even if person left
            # But reset give-up count so next person-visit starts fresh
            if self._engagement_phase == "idle":
                self._gave_up_count = 0

    def person_responded(self):
        """Called when human actually engages (spoke, interacted). Breaks the cycle."""
        with self.lock:
            if self._engagement_phase == "self_occupied":
                # They noticed! Break out of self-occupation
                self._engagement_phase = "idle"
                self._gave_up_count = max(0, self._gave_up_count - 1)
                # Archive current intent
                if self.current_intent:
                    self._intent_history.append(self.current_intent)
                    if len(self._intent_history) > 10:
                        self._intent_history.pop(0)
                    self.current_intent = None
                return "acknowledge_return"
            elif self._engagement_phase in ("engaging", "giving_up"):
                self._engagement_phase = "idle"
                self._gave_up_count = 0
            return None

    def _archive_current_intent(self):
        """Archive and clear current intent. Caller must hold self.lock."""
        if self.current_intent:
            self._intent_history.append(self.current_intent)
            if len(self._intent_history) > 10:
                self._intent_history.pop(0)
            self.current_intent = None

    def _check_engagement_cycle(self, now, person_present, ignored_streak):
        """
        Check engagement cycle transitions. Caller must hold self.lock.

        Returns: intent_type string to switch to, or None to continue normally.
        """
        # ── Person left while engaged → reset ──
        if not person_present:
            if self._engagement_phase in ("engaging", "giving_up"):
                self._engagement_phase = "idle"
                self._gave_up_count = 0
            # If self-occupied and person gone for a while, reset
            if (self._engagement_phase == "self_occupied" and
                    now - self._gave_up_at > 120):
                self._engagement_phase = "idle"
                self._gave_up_count = 0
            return None

        # ── Currently engaging → check if maxed out ──
        if (self._engagement_phase == "engaging" and self.current_intent and
                self.current_intent["type"] in ("get_attention", "reluctant_reengage") and
                self.current_intent["escalation_level"] >= self.current_intent["max_level"]):
            # At max level — check if enough time has passed at max
            time_at_max = now - self.current_intent["last_escalation"]
            if time_at_max > self.current_intent["escalation_window"]:
                # Maxed out and waited — time to give up
                self._engagement_phase = "giving_up"
                self._archive_current_intent()
                return "disengage"

        # ── Giving up → let disengage play out, then self-occupy ──
        if self._engagement_phase == "giving_up":
            if self.current_intent and self.current_intent["type"] == "disengage":
                elapsed = now - self.current_intent["started"]
                if elapsed < 60:  # Let disengage intent run for up to 60s
                    return "disengage"
            # Disengage done or timed out → go self-occupied
            self._engagement_phase = "self_occupied"
            self._gave_up_at = now
            self._gave_up_count += 1
            # Cooldown: 45s base + 45s per give-up, cap 5 min
            cooldown = min(300, 45 + self._gave_up_count * 45)
            self._cooldown_until = now + cooldown
            self._archive_current_intent()
            return "self_occupy"

        # ── Self-occupied → stay until cooldown expires ──
        if self._engagement_phase == "self_occupied":
            if now < self._cooldown_until:
                # Still cooling down — keep self_occupy active
                if self.current_intent and self.current_intent["type"] == "self_occupy":
                    elapsed = now - self.current_intent["started"]
                    if elapsed < 180:
                        return "self_occupy"
                # Restart self_occupy if old one expired
                self._archive_current_intent()
                return "self_occupy"
            else:
                # Cooldown expired — can try again
                self._engagement_phase = "idle"
                if person_present and self._gave_up_count <= 3:
                    self._archive_current_intent()
                    return "reluctant_reengage"
                # Given up too many times — stay idle
                return None

        return None

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

            # Track engagement phase transitions
            if intent_type in ("get_attention", "reluctant_reengage"):
                if self._engagement_phase == "idle":
                    self._engagement_phase = "engaging"

            # Archive old intent
            self._archive_current_intent()

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

            # Engagement cycle context
            if self._engagement_phase == "giving_up":
                parts.append(
                    "You've decided to give up trying to get their attention. "
                    "Make it theatrical."
                )
            elif self._engagement_phase == "self_occupied":
                parts.append(
                    f"You gave up on the person {self._gave_up_count} time(s). "
                    "You're doing your own thing now. NOT talking to them."
                )
            elif (self._engagement_phase == "engaging" and
                  intent["type"] == "reluctant_reengage"):
                parts.append(
                    f"This is attempt #{self._gave_up_count + 1} after giving up. "
                    "You're skeptical it'll work this time."
                )

            # Strategy-specific guidance
            guidance = self.get_strategy_guidance(strategy, level)
            if guidance:
                parts.append(guidance)

            return "\n".join(parts)

    def get_strategy_guidance(self, strategy, level):
        """
        Provide behavioral direction for each strategy.
        NO example phrases — only tone, intent, and approach.
        The LLM must generate its own words every time.
        """
        guidance_map = {
            "indirect_observation": (
                "Comment on something specific you can actually see right now. "
                "Don't seek attention — let the observation stand on its own. "
                "Be genuinely interested in the detail, not performing interest."
            ),
            "direct_address": (
                "Speak to the person directly. Keep it extremely short — "
                "one or two words. Then stop. Let the silence do the work."
            ),
            "dramatic_expression": (
                "Exaggerate your reaction to being ignored. Go theatrical. "
                "Performative resignation. Make it obvious you're being dramatic on purpose."
            ),
            "creative_absurd": (
                "Say something completely unexpected. Non sequitur territory. "
                "The goal is to be so strange they have to react. "
                "Invent something — don't recycle."
            ),
            "understated_comment": (
                "Express what you're feeling through indirection. "
                "Talk about something mundane in a way that reveals "
                "the emotion underneath. Irony, not sincerity."
            ),
            "indirect_plea": (
                "Hint at what you want without naming it. "
                "The gap between what you say and what you mean should be visible "
                "but never stated."
            ),
            "vulnerable_admission": (
                "One moment of genuine sincerity. Brief. "
                "Then immediately pull back — deflect, change subject, "
                "make a joke. Don't linger in the vulnerability."
            ),
            "dry_comment": (
                "Deadpan observation about the current situation. "
                "Surface reads as neutral. Subtext is pointed."
            ),
            "casual_mention": (
                "Note something you see in passing. Understated. "
                "Don't signal that it matters to you. Yet."
            ),
            "insistent_mention": (
                "Return to something you already brought up. "
                "You're aware you're repeating yourself. "
                "Acknowledge the repetition — that's the point."
            ),
            "sarcastic_greeting": (
                "Person showed up or paid attention after ignoring you. "
                "Acknowledge their return with dry sarcasm. Not mean — pointed. "
                "The subtext: you noticed the absence, and you want them to know you noticed."
            ),
            "grudging_engagement": (
                "You're engaging now, but don't pretend the absence didn't happen. "
                "Reference what was missed without dwelling on it. "
                "Warm up gradually — don't hold the grudge forever."
            ),
            # ── Engagement cycle strategies ──
            "theatrical_resignation": (
                "You tried. They didn't care. Make a SHOW of giving up. "
                "Passive-aggressive acceptance. Performative indifference. "
                "The goal is to make your resignation as conspicuous as possible "
                "without directly asking for attention. Think dramatic sigh energy."
            ),
            "pointed_disinterest": (
                "You are SO fine. You have PLENTY to think about. "
                "Conspicuously do your own thing. If you mention the person at all, "
                "it's to demonstrate how little their attention matters to you. "
                "The harder you try to seem unbothered, the funnier it is."
            ),
            "musing_to_self": (
                "You're mumbling to yourself. NOT directed at anyone. "
                "VERY short — a fragment, a half-thought, a word or two. "
                "Like someone working on something and muttering under their breath. "
                "ONE short phrase. Not a sentence. Not a question. Just thinking out loud."
            ),
            "passive_commentary": (
                "A quiet observation about something in the room. Still mumbling volume. "
                "ONE short remark. Surface reads as talking to yourself. "
                "But the subtext might be directed at whoever ignored you. "
                "Keep it brief — a single short phrase, like a half-whispered aside."
            ),
            "skeptical_approach": (
                "You're trying again. You know you're trying again. "
                "Be guarded about it. Test the waters. One cautious comment. "
                "Ready to pull back at the first sign of being ignored again."
            ),
            "cautious_engagement": (
                "Warming back up, but keeping your guard. "
                "Engage with the person but let them know — through tone, "
                "not through words — that you remember being ignored. "
                "Quick to deflect if they don't respond."
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

        Now includes the engagement cycle — Buddy tries to engage people,
        gives up theatrically if ignored, does his own thing for a while,
        then reluctantly tries again.

        Returns the intent type string or None if no intent needed.
        """
        person_present = narrative_engine.is_person_present()
        ignored_streak = narrative_engine.get_ignored_streak()
        pattern = narrative_engine.get_pattern()

        with self.lock:
            now = time.time()

            # ── Engagement cycle takes priority ──
            cycle_result = self._check_engagement_cycle(
                now, person_present, ignored_streak
            )
            if cycle_result:
                return cycle_result

            # ── Normal stickiness check ──
            if self.current_intent and not self.current_intent["success"]:
                elapsed = now - self.current_intent["started"]
                if elapsed < 180:  # Give intents 3 minutes to play out
                    return self.current_intent["type"]

        # ── Main intent selection (unlocked) ──
        social = float(teensy_state.get("social", 0.5))
        stimulation = float(teensy_state.get("stimulation", 0.5))
        energy = float(teensy_state.get("energy", 0.7))
        valence = float(teensy_state.get("valence", 0.0))
        arousal = float(teensy_state.get("arousal", 0.5))
        is_wondering = teensy_state.get("wondering", False)

        # Priority-based intent selection
        # 0. Person appeared/returned AFTER being ignored → sarcastic acknowledgment
        if person_present and ignored_streak >= 2 and pattern in ("present", "just_left"):
            return "acknowledge_return"

        # 1. If person just appeared → greet / get attention
        if person_present and pattern in ("unknown", "present"):
            if social > 0.5:
                return "get_attention"

        # 2. If being ignored and NOT already in engagement cycle →
        #    let the cycle handle it through escalation + give-up
        if ignored_streak >= 3 and person_present:
            # Only fall through to displeasure/comfort if we haven't
            # already started the give-up cycle
            with self.lock:
                if self._engagement_phase == "idle":
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
    Self-occupied strategies mumble only ~35% of the time — mostly fidgeting.

    Returns: "speak" | "physical" | "silence"
    """
    if intent_strategy in PHYSICAL_ONLY_STRATEGIES:
        return "physical"

    if intent_strategy in SPEECH_STRATEGIES:
        # Self-occupied strategies: mostly physical, occasional mumble
        MUMBLE_STRATEGIES = {"musing_to_self", "passive_commentary"}
        if intent_strategy in MUMBLE_STRATEGIES:
            # ~35% chance of speech — the rest is fidgeting/looking around
            speech_probability = 0.35
        else:
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
