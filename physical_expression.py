"""
physical_expression.py — Speech Performance Arc & Physical Expressions
======================================================================

Handles two things:
1. Physical expressions — non-verbal alternatives to speech (~30-40% of the time
   when speech would fire, Buddy expresses physically instead).
2. Speech performance arc — the movement sequence around speech that makes
   utterances feel like a creature deciding to communicate, not just audio output.

PERFORMANCE ARC:
  1. INTENTION (200-500ms) — Orient toward person, "inhale" movement
  2. DELIVERY (during speech) — Movement accompanies words
  3. WATCHING (500ms-3s) — Holds attention, watches for reaction
  4. RESOLUTION (based on response) — React to response or lack thereof

PHYSICAL EXPRESSIONS (no speech):
  - sigh: slight drop/settle, slow
  - double_take: quick look away then back
  - pointed_look: orient toward something specific, hold
  - settle: sink deeper into rest position
  - expectant_look: orient toward person, slight lean forward, wait
  - startled_glance: quick movement toward stimulus
  - dismissive_turn: slow turn away from person
  - curious_tilt: head tilt to one side, hold
"""

import time
import random
import threading


# ═══════════════════════════════════════════════════════
# PHYSICAL EXPRESSION DEFINITIONS
# ═══════════════════════════════════════════════════════

PHYSICAL_EXPRESSIONS = {
    "sigh": {
        "description": "Slight drop/settle — like deflating",
        "commands": [
            ("LOOK:{base},{nod_down}", 0.8),    # Sink down
            ("wait", 1.0),                        # Hold
            ("LOOK:{base},{nod_rest}", 0.6),     # Slowly return
        ],
        "mood": "deflated",
        "nod_offset": 8,  # How much to drop nod
    },
    "double_take": {
        "description": "Quick look away then back — something caught attention",
        "commands": [
            ("LOOK:{base_away},{nod}", 0.2),    # Quick look away
            ("wait", 0.3),                        # Beat
            ("LOOK:{base},{nod}", 0.15),         # Snap back
            ("wait", 0.5),                        # Hold on target
        ],
        "mood": "surprised",
        "base_away_offset": 30,
    },
    "pointed_look": {
        "description": "Orient toward something specific, hold — 'I see that'",
        "commands": [
            ("LOOK:{target_base},{target_nod}", 0.5),  # Look at thing
            ("wait", 2.0),                               # Stare at it
            ("LOOK:{base},{nod}", 0.4),                  # Look back
        ],
        "mood": "observant",
    },
    "settle": {
        "description": "Sink deeper into rest — giving up, getting comfortable",
        "commands": [
            ("LOOK:{base},{nod_down}", 1.2),    # Slowly sink
            ("wait", 0.5),                        # Stay
        ],
        "mood": "resigned",
        "nod_offset": 12,
    },
    "expectant_look": {
        "description": "Orient toward person, slight lean forward — waiting",
        "commands": [
            ("LOOK:90,{nod_up}", 0.4),          # Look at person, lean in
            ("wait", 2.5),                        # Hold expectantly
            ("LOOK:90,{nod}", 0.3),              # Return
        ],
        "mood": "expectant",
        "nod_offset": 10,
    },
    "startled_glance": {
        "description": "Quick movement toward stimulus — reactive",
        "commands": [
            ("EXPRESS:startled", 0),              # Startle expression
            ("wait", 0.8),
        ],
        "mood": "startled",
    },
    "dismissive_turn": {
        "description": "Slow turn away — 'fine, whatever'",
        "commands": [
            ("LOOK:{base_away},{nod}", 1.5),    # Slow turn away
            ("wait", 2.0),                        # Hold looking away
        ],
        "mood": "dismissive",
        "base_away_offset": 40,
    },
    "curious_tilt": {
        "description": "Head tilt to one side — 'hmm, interesting'",
        "commands": [
            ("EXPRESS:curious", 0),               # Curious expression
            ("wait", 1.5),                        # Hold
        ],
        "mood": "curious",
    },
    # ── Self-occupied / disengagement expressions ──
    "restless_scan": {
        "description": "Look around the room slowly — 'I have my own things to look at'",
        "commands": [
            ("LOOK:{base_away},{nod}", 1.0),     # Look one direction
            ("wait", 1.5),                        # Linger
            ("LOOK:{base},{nod_up}", 0.8),        # Look up/other direction
            ("wait", 1.0),                        # Linger
            ("LOOK:{base},{nod}", 0.5),            # Return
        ],
        "mood": "disengaged",
        "base_away_offset": 35,
        "nod_offset": 10,
    },
    "conspicuous_settle": {
        "description": "Settle in dramatically — 'I'm FINE right here, thanks'",
        "commands": [
            ("LOOK:{base},{nod_down}", 0.6),     # Sink down
            ("wait", 0.5),                        # Pause
            ("LOOK:{base},{nod_down}", 0.3),      # Sink a bit more
            ("wait", 2.0),                        # Hold — conspicuously comfortable
        ],
        "mood": "resigned",
        "nod_offset": 15,
    },
    "fidgety_shift": {
        "description": "Restless small movements — can't quite get comfortable",
        "commands": [
            ("LOOK:{base_away},{nod}", 0.3),     # Quick shift
            ("wait", 0.4),
            ("LOOK:{base},{nod_down}", 0.3),      # Adjust
            ("wait", 0.3),
            ("LOOK:{base},{nod}", 0.2),            # Settle back
            ("wait", 0.5),
        ],
        "mood": "restless",
        "base_away_offset": 15,
        "nod_offset": 5,
    },
    # ── Attention-triggered expressions ──
    "attention_ready": {
        "description": "Subtle perk-up — 'I see you looking at me'",
        "commands": [
            ("LOOK:90,{nod_up}", 0.3),           # Slight lean forward toward person
            ("wait", 0.4),                         # Brief hold — shows acknowledgment
        ],
        "mood": "attentive",
        "nod_offset": 5,  # Small movement — subtle, not dramatic
    },
}

# Map emotional states to appropriate physical expressions
EMOTION_TO_EXPRESSION = {
    "lonely": ["sigh", "settle", "expectant_look"],
    "bored": ["sigh", "settle", "dismissive_turn"],
    "curious": ["double_take", "pointed_look", "curious_tilt"],
    "content": ["settle"],
    "anxious": ["startled_glance", "double_take"],
    "ignored": ["dismissive_turn", "sigh", "pointed_look"],
    "wanting_attention": ["expectant_look", "double_take", "curious_tilt"],
    "startled": ["startled_glance"],
    "playful": ["double_take", "curious_tilt"],
    # Engagement cycle emotions
    "disengaged": ["dismissive_turn", "restless_scan", "conspicuous_settle"],
    "self_occupied": ["restless_scan", "curious_tilt", "fidgety_shift"],
    "resigned": ["conspicuous_settle", "sigh", "settle"],
    "reluctant": ["expectant_look", "curious_tilt"],
    "attentive": ["attention_ready"],
}


class PhysicalExpressionManager:
    """
    Manages physical expressions and the speech performance arc.
    Bridges between the intent system and Teensy servo commands.
    """

    def __init__(self):
        self.lock = threading.Lock()
        self._last_expression_time = 0
        self._min_expression_gap = 15  # seconds between physical expressions

    def select_expression(self, emotional_context):
        """
        Choose an appropriate physical expression for the current emotional state.

        Args:
            emotional_context: string like "lonely", "bored", "curious", etc.

        Returns: expression name (key into PHYSICAL_EXPRESSIONS) or None
        """
        with self.lock:
            now = time.time()
            if now - self._last_expression_time < self._min_expression_gap:
                return None

            candidates = EMOTION_TO_EXPRESSION.get(
                emotional_context,
                ["sigh", "settle", "curious_tilt"]  # fallback
            )

            if not candidates:
                return None

            choice = random.choice(candidates)
            self._last_expression_time = now
            return choice

    def get_expression_commands(self, expression_name, current_base=90,
                                 current_nod=115, target_base=None,
                                 target_nod=None):
        """
        Get the sequence of Teensy commands for a physical expression.

        Returns: list of (command_string, delay_seconds) tuples
        """
        expr = PHYSICAL_EXPRESSIONS.get(expression_name)
        if not expr:
            return []

        commands = []
        nod_offset = expr.get("nod_offset", 8)
        base_away_offset = expr.get("base_away_offset", 30)

        nod_down = min(150, current_nod + nod_offset)
        nod_up = max(80, current_nod - nod_offset)
        nod_rest = current_nod

        # Decide which direction to look away
        if current_base > 90:
            base_away = max(10, current_base - base_away_offset)
        else:
            base_away = min(170, current_base + base_away_offset)

        t_base = target_base or current_base
        t_nod = target_nod or current_nod

        for cmd_template, delay in expr["commands"]:
            cmd = cmd_template.format(
                base=current_base,
                nod=current_nod,
                nod_down=nod_down,
                nod_up=nod_up,
                nod_rest=nod_rest,
                base_away=base_away,
                target_base=t_base,
                target_nod=t_nod,
            )
            commands.append((cmd, delay))

        return commands

    def get_attention_ready_commands(self, current_base=90, current_nod=115):
        """
        Get commands for the subtle "I see you" ready signal.
        Bypasses the normal expression cooldown — this is a feedback signal, not
        a narrative expression.

        Returns: list of (command_string, delay_seconds) tuples
        """
        return self.get_expression_commands(
            "attention_ready",
            current_base=current_base,
            current_nod=current_nod
        )

    # ═══════════════════════════════════════════════════════
    # SPEECH PERFORMANCE ARC
    # ═══════════════════════════════════════════════════════

    def get_pre_speech_arc(self, arousal, valence, intent_strategy=None):
        """
        Get the "intention" phase commands — what happens BEFORE speech.
        The "inhale before speaking" beat.

        Returns: list of (command_string, delay_seconds) tuples
        """
        commands = []

        # 1. Orient toward person (if not already looking at them)
        commands.append(("ATTENTION:center", 0.3))

        # 2. Slight "intake" movement — lean forward slightly
        nod_lean = 108  # Slightly forward from rest (115)
        commands.append((f"LOOK:90,{nod_lean}", 0.2))

        # 3. Pre-speech pause — dramatic weight proportional to emotional importance
        if intent_strategy in ("dramatic_expression", "vulnerable_admission",
                               "direct_statement"):
            # Heavy statement coming — longer pause
            pause = random.uniform(1.5, 3.0)
        elif intent_strategy in ("creative_absurd", "witty_observation"):
            # Comedy timing — quick beat
            pause = random.uniform(0.3, 0.8)
        elif arousal > 0.7:
            # Excited — minimal pause
            pause = random.uniform(0.1, 0.3)
        elif valence < -0.3:
            # Negative mood — heavier pause
            pause = random.uniform(0.8, 2.0)
        else:
            # Normal — moderate pause
            pause = random.uniform(0.2, 0.8)

        commands.append(("wait", pause))

        return commands

    def get_post_speech_arc(self, response_expected=True):
        """
        Get the "watching" phase commands — what happens AFTER speech.
        Buddy holds attention and waits for response.

        Returns: list of (command_string, delay_seconds) tuples
        """
        commands = []

        if response_expected:
            # Hold attention on person — slight lean forward
            commands.append(("LOOK:90,108", 0.3))  # Attentive pose
            # Wait for response (small movements during wait)
            commands.append(("wait", 2.0))
        else:
            # Not expecting response — gentle settle back
            commands.append(("LOOK:90,115", 0.5))
            commands.append(("wait", 0.5))

        return commands

    def get_resolution_arc(self, response_type):
        """
        Get resolution commands based on how the human responded.

        response_type: "responded" / "looked" / "smiled" / "laughed" /
                       "ignored" / "left"
        """
        commands = []

        if response_type in ("smiled", "laughed"):
            # Positive response — nod/small celebration
            commands.append(("ACKNOWLEDGE", 0))
            commands.append(("wait", 0.5))

        elif response_type == "responded" or response_type == "looked":
            # Acknowledged — settle back contentedly
            commands.append(("LOOK:90,115", 0.3))
            commands.append(("wait", 0.3))

        elif response_type == "ignored":
            # Ignored — visible deflation
            commands.append(("LOOK:90,125", 0.8))  # Gaze drops
            commands.append(("wait", 0.5))
            commands.append(("LOOK:90,118", 0.4))   # Settle

        elif response_type == "left":
            # Person left — track them, then slowly return
            commands.append(("ATTENTION:left", 0.5))  # Follow them
            commands.append(("wait", 1.5))
            commands.append(("ATTENTION:center", 0.8))  # Slow return

        return commands


# ═══════════════════════════════════════════════════════
# TIMING AS EXPRESSION
# ═══════════════════════════════════════════════════════

def calculate_speech_delay(arousal, valence, intent_strategy, ignored_streak):
    """
    Calculate how long to wait AFTER deciding to speak but BEFORE starting.
    This random delay breaks the "threshold = instant speech" pattern.

    Returns: delay in seconds
    """
    # Base delay: 10-60 seconds (as specified in the prompt)
    # But modulate based on urgency
    if intent_strategy in ("direct_address", "startled_glance"):
        # Urgent — short delay
        base_delay = random.uniform(2, 10)
    elif intent_strategy in ("dramatic_expression", "creative_absurd"):
        # Building to something — moderate delay
        base_delay = random.uniform(8, 25)
    elif intent_strategy in ("vulnerable_admission",):
        # Working up to it — longer delay
        base_delay = random.uniform(15, 45)
    elif intent_strategy in ("musing_to_self", "passive_commentary"):
        # Self-occupied mumbling — long gaps between mumbles
        # Creates the intermittent effect: fidget... fidget... mumble... fidget...
        base_delay = random.uniform(20, 60)
    elif intent_strategy in ("theatrical_resignation", "pointed_disinterest"):
        # Giving up — moderate pause before the dramatic moment
        base_delay = random.uniform(5, 15)
    else:
        # Normal — standard range
        base_delay = random.uniform(10, 40)

    # High arousal shortens the delay (can't hold it in)
    arousal_factor = 1.0 - (arousal * 0.4)  # 0.6x at max arousal

    # Being ignored LENGTHENS delay — Buddy gets quieter, withdraws
    # (sulking behavior: not more persistent, but more reluctant)
    ignore_factor = 1.0 + (ignored_streak * 0.25)  # 1.25x, 1.5x, 1.75x, 2.0x...
    ignore_factor = min(3.0, ignore_factor)  # Cap at 3x

    delay = base_delay * arousal_factor * ignore_factor

    # Clamp to reasonable range
    return max(3.0, min(90.0, delay))
