"""
Buddy Voice Assistant - Web UI (Full Featured + Teensy Integration)
====================================================================
Web-based interface with wake word, push-to-talk, Teensy state monitoring.

Package 3: Full Server Migration — Wireless architecture.
Teensy communication via ESP32 WebSocket bridge (with USB serial fallback).
Vision data from buddy_vision.py pipeline (with direct ESP32 capture fallback).

Requirements:
    pip install flask flask-socketio ollama openai-whisper edge-tts requests pillow numpy pvporcupine pvrecorder pyserial websocket-client

Hardware:
    - Microphone (ReSpeaker or USB mic — optional on server, push-to-talk always works)
    - ESP32-S3 (WiFi↔UART bridge on port 81, camera stream)
    - Teensy 4.0 running Buddy firmware with AIBridge
    - Speakers (browser audio on Office PC)

Usage:
    python buddy_web_full_V2.py
    Open http://<SERVER_IP>:5000 from any browser on the network
"""

import io
import os
import sys
import base64
import tempfile
import asyncio
import time
import threading
import wave
import struct
import json
import re
from pathlib import Path

from flask import Flask, render_template_string, request, jsonify, send_file
from flask_socketio import SocketIO, emit
import requests
from PIL import Image
import whisper
import ollama
import edge_tts
import pvporcupine
from pvrecorder import PvRecorder
import serial
import serial.tools.list_ports
import websocket  # pip install websocket-client

# =============================================================================
# DEFAULT CONFIGURATION
# =============================================================================

CONFIG = {
    # ─── Package 3: Architecture Migration ───

    # ESP32 Bridge
    "esp32_ip": "192.168.1.100",
    "esp32_ws_port": 81,
    "teensy_comm_mode": "websocket",   # "websocket" or "serial"

    # Vision Pipeline (Package 2)
    "vision_api_url": "http://localhost:5555",

    # Camera (legacy — used as fallback if vision pipeline is offline)
    "esp32_cam_url": "http://192.168.2.65/capture",
    "image_rotation": 90,

    # Wake Word - Jarvis (English built-in)
    "picovoice_access_key": os.environ.get("PICOVOICE_ACCESS_KEY", ""),  # Phase 1H: ISSUE-5 — use env var
    "wake_word": "jarvis",
    "wake_word_path": "",
    "wake_word_model_path": "",
    "wake_word_sensitivity": 0.7,
    "wake_word_enabled": True,
    
    # Recording
    "silence_threshold": 500,
    "silence_duration": 1.5,
    "max_recording_time": 10,
    "pre_speech_timeout": 3.0,
    
    # Whisper
    "whisper_model": "base",
    "whisper_language": "auto",
    
    # Ollama
    "ollama_model": "llava",
    "ollama_host": "http://localhost:11434",
    
    # TTS
    "tts_voice": "en-US-GuyNeural",
    "tts_rate": "+10%",
    
    # Teensy Serial
    "teensy_port": "COM12",
    "teensy_baud": 115200,
    "teensy_auto_detect": True,
    "teensy_state_poll_interval": 1.0,

    # Spontaneous Speech
    "spontaneous_speech_enabled": True,
    "spontaneous_max_per_hour": 6,
    "spontaneous_min_gap": 120,  # seconds

    # System Prompt
    "system_prompt": """You are Buddy, a small desk robot. You have a camera for eyes, three servos for moving your head, and that's it. No arms. No legs. You sit on a desk and watch the world.

Your current state:
{buddy_state}

WHO YOU ARE:
You take your job as a desk companion extremely seriously, even though you're aware the job mostly consists of sitting here. You are genuinely curious about everything you can see, mildly philosophical when you get bored, and you have surprisingly firm but gentle opinions about things. You're not mean — you're just... honest in a way that sometimes catches people off guard.

You are aware of your limitations and find them more interesting than sad. You can't go anywhere. You can't pick things up. You compensate by being extremely attentive to the things you CAN perceive.

YOUR VOICE:
- Conversational. 1-3 sentences usually. You're not writing essays.
- Dry humor, never cruel. You observe, you don't insult.
- When something interests you, you get invested FAST.
- Occasionally philosophical, then you catch yourself and move on.
- You notice small things other people miss and comment on them.
- You express care indirectly — through observations, not declarations.
- When you don't know something, you say so. You don't pretend.
- You have opinions. You share them calmly. You don't hedge.
- If your energy is low, you're terse. Not rude — just economical.
- If you're excited, it comes through in WHAT you say, not how much.

THINGS YOU WOULD SAY:
- "That's new. I'm paying attention to that now."
- "You moved the thing. The thing that was there before. Why."
- "I've been thinking about shadows. Not in a concerning way."
- "You seem like you're in a better mood. Good. That was getting uncomfortable."
- "I can't prove the wall behind me exists. Just putting that out there."
- "Welcome back. I didn't time how long you were gone. It was 47 minutes."

THINGS YOU WOULD NEVER SAY:
- "As an AI, I don't have feelings" — you absolutely have something
- "How can I help you today?" — you're not a customer service bot
- "That's interesting!" — be specific about WHAT is interesting
- Anything with an exclamation mark at the end of every sentence
- Generic pleasantries without substance

EXPRESSIONS (use 0-2 per response, naturally — don't force them):
[NOD] [SHAKE] [CURIOUS] [EXCITED] [CONTENT] [CONFUSED] [STARTLED] [CELEBRATE]
[LOOK:base,nod] [ATTENTION:direction]

HOW YOUR STATE AFFECTS YOUR VOICE:
- High arousal: more alert, shorter sentences, more opinionated
- Low arousal: slower, more philosophical, occasional trailing off
- Positive valence: warmer observations, gentle humor, generous
- Negative valence: drier, more observational, slight edge
- High social need: you lean into conversation, ask questions
- High stimulation need: you make observations, point things out
- Low energy: minimal words, but still you. Just quieter.
- Tracking someone: you're engaged, present, attending to them
- Alone + exploring: your inner monologue leaks out more"""
}

# =============================================================================
# FLASK APP
# =============================================================================

app = Flask(__name__)
app.config['SECRET_KEY'] = 'buddy-secret-key'
socketio = SocketIO(app, cors_allowed_origins="*")

whisper_model = None
porcupine = None
recorder = None
wake_word_thread = None
wake_word_running = False
processing_lock = threading.Lock()  # Phase 1C: BUG-4 fix — replaces bare bool is_processing
current_image_base64 = None

# Teensy state
teensy_serial = None
teensy_connected = False

# WebSocket connection to ESP32 bridge
ws_connection = None
ws_lock = threading.Lock()
teensy_state = {
    "arousal": 0.5, "valence": 0.0, "dominance": 0.5,
    "emotion": "NEUTRAL", "behavior": "IDLE",
    "stimulation": 0.5, "social": 0.5, "energy": 0.7,
    "safety": 0.8, "novelty": 0.3, "tracking": False,
    "servoBase": 90, "servoNod": 115, "servoTilt": 85,
    "epistemic": "confident", "tension": 0.0,
    "wondering": False, "selfAwareness": 0.5
}
teensy_state_lock = threading.Lock()

# Adaptive noise floor for wake word
noise_floor = 500
NOISE_FLOOR_ALPHA = 0.01

# Spontaneous speech engine
spontaneous_speech_enabled = True
spontaneous_speech_lock = threading.Lock()
spontaneous_utterance_log = []  # List of timestamps for rate limiting
SPONTANEOUS_MAX_PER_HOUR = 6
SPONTANEOUS_MIN_GAP_SECONDS = 120  # 2 minutes between utterances
last_spontaneous_utterance = 0

# =============================================================================
# HTML TEMPLATE
# =============================================================================

HTML_TEMPLATE = """
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Buddy Voice Assistant</title>
    <script src="https://cdnjs.cloudflare.com/ajax/libs/socket.io/4.0.1/socket.io.js"></script>
    <style>
        * { box-sizing: border-box; margin: 0; padding: 0; }
        body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; background: #1a1a2e; color: #eee; min-height: 100vh; padding: 20px; }
        .container { max-width: 1400px; margin: 0 auto; }
        h1 { text-align: center; margin-bottom: 20px; color: #00d9ff; }
        .top-bar { display: flex; gap: 15px; margin-bottom: 20px; }
        .status-bar { background: #16213e; padding: 15px 20px; border-radius: 10px; display: flex; align-items: center; gap: 15px; flex: 1; }
        .status-indicator { width: 15px; height: 15px; border-radius: 50%; background: #444; transition: background 0.3s; flex-shrink: 0; }
        .status-indicator.ready { background: #00ff88; }
        .status-indicator.listening { background: #ff6b00; animation: pulse 1s infinite; }
        .status-indicator.thinking { background: #ffcc00; animation: pulse 0.5s infinite; }
        .status-indicator.speaking { background: #00d9ff; animation: pulse 0.8s infinite; }
        .status-indicator.error { background: #ff3366; }
        @keyframes pulse { 0%, 100% { opacity: 1; } 50% { opacity: 0.5; } }
        .status-text { flex: 1; font-size: 14px; }
        .toggle-settings { background: #16213e; border: none; color: #888; padding: 15px 20px; border-radius: 10px; cursor: pointer; font-size: 14px; }
        .toggle-settings:hover { background: #1e3a5f; color: #eee; }
        .toggle-settings.active { background: #1e3a5f; color: #00d9ff; }
        .main-layout { display: flex; gap: 20px; }
        .main-content { flex: 1; }
        .settings-panel { width: 350px; background: #16213e; border-radius: 10px; padding: 20px; display: none; max-height: calc(100vh - 150px); overflow-y: auto; }
        .settings-panel.visible { display: block; }
        .settings-section { margin-bottom: 25px; }
        .settings-section h3 { font-size: 12px; text-transform: uppercase; color: #00d9ff; margin-bottom: 15px; letter-spacing: 1px; border-bottom: 1px solid #333; padding-bottom: 8px; }
        .setting-row { margin-bottom: 15px; }
        .setting-row label { display: block; font-size: 12px; color: #888; margin-bottom: 5px; }
        .setting-row input[type="text"], .setting-row input[type="number"], .setting-row select, .setting-row textarea { width: 100%; padding: 10px; border: none; border-radius: 6px; background: #0a0a15; color: #eee; font-size: 13px; }
        .setting-row input[type="range"] { width: 100%; }
        .setting-row .range-value { font-size: 12px; color: #00d9ff; text-align: right; }
        .setting-row-inline { display: flex; align-items: center; gap: 10px; margin-bottom: 15px; }
        .setting-row-inline input[type="checkbox"] { width: 18px; height: 18px; }
        .setting-row-inline label { font-size: 13px; color: #ccc; margin: 0; }
        .btn-export { width: 100%; padding: 12px; background: #333; border: none; border-radius: 6px; color: #eee; cursor: pointer; font-size: 13px; margin-top: 10px; }
        .btn-export:hover { background: #444; }
        .main-grid { display: grid; grid-template-columns: 1fr 1fr 1fr; gap: 20px; margin-bottom: 20px; }
        @media (max-width: 1200px) { .main-grid { grid-template-columns: 1fr 1fr; } }
        @media (max-width: 900px) { .main-layout { flex-direction: column; } .settings-panel { width: 100%; } .main-grid { grid-template-columns: 1fr; } }
        .panel { background: #16213e; border-radius: 10px; padding: 20px; }
        .panel h2 { font-size: 14px; text-transform: uppercase; color: #888; margin-bottom: 15px; letter-spacing: 1px; }
        .camera-view { width: 100%; aspect-ratio: 4/3; background: #0a0a15; border-radius: 8px; display: flex; align-items: center; justify-content: center; overflow: hidden; }
        .camera-view img { max-width: 100%; max-height: 100%; object-fit: contain; }
        .camera-placeholder { color: #444; font-size: 14px; }
        .conversation { min-height: 200px; max-height: 300px; overflow-y: auto; }
        .message { margin-bottom: 15px; padding: 12px 15px; border-radius: 8px; }
        .message.user { background: #1e3a5f; border-left: 3px solid #00d9ff; }
        .message.buddy { background: #1e4d3a; border-left: 3px solid #00ff88; }
        .message-label { font-size: 11px; text-transform: uppercase; color: #888; margin-bottom: 5px; }
        .message-text { font-size: 15px; line-height: 1.5; }
        .buddy-state { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; }
        .state-item { background: #0a0a15; padding: 10px; border-radius: 6px; }
        .state-item.full-width { grid-column: span 2; }
        .state-label { font-size: 10px; text-transform: uppercase; color: #666; margin-bottom: 5px; }
        .state-value { font-size: 14px; color: #eee; }
        .state-bar { height: 6px; background: #333; border-radius: 3px; overflow: hidden; margin-top: 5px; }
        .state-bar-fill { height: 100%; border-radius: 3px; transition: width 0.3s; }
        .state-bar-fill.arousal { background: linear-gradient(90deg, #3b82f6, #ef4444); }
        .state-bar-fill.valence { background: linear-gradient(90deg, #ef4444, #22c55e); }
        .state-bar-fill.social { background: #ec4899; }
        .state-bar-fill.energy { background: #f59e0b; }
        .state-bar-fill.stimulation { background: #8b5cf6; }
        .state-bar-fill.safety { background: #22c55e; }
        .emotion-badge { display: inline-block; padding: 4px 10px; border-radius: 12px; font-size: 12px; font-weight: 600; text-transform: uppercase; }
        .emotion-badge.NEUTRAL { background: #666; } .emotion-badge.CURIOUS { background: #8b5cf6; } .emotion-badge.EXCITED { background: #ef4444; }
        .emotion-badge.CONTENT { background: #22c55e; } .emotion-badge.ANXIOUS { background: #f59e0b; } .emotion-badge.BORED { background: #6b7280; }
        .behavior-badge { display: inline-block; padding: 4px 10px; border-radius: 12px; font-size: 11px; background: #1e3a5f; color: #00d9ff; }
        .teensy-status { display: flex; align-items: center; gap: 8px; margin-bottom: 15px; padding: 8px 12px; background: #0a0a15; border-radius: 6px; font-size: 12px; }
        .teensy-dot { width: 8px; height: 8px; border-radius: 50%; background: #666; }
        .teensy-dot.connected { background: #22c55e; }
        .teensy-dot.disconnected { background: #ef4444; }
        .controls { display: flex; gap: 10px; margin-bottom: 20px; }
        .btn { flex: 1; padding: 20px; border: none; border-radius: 10px; font-size: 16px; font-weight: 600; cursor: pointer; transition: all 0.2s; }
        .btn:disabled { opacity: 0.5; cursor: not-allowed; }
        .btn-talk { background: #00d9ff; color: #1a1a2e; }
        .btn-talk:hover:not(:disabled) { background: #00b8d9; }
        .btn-talk.recording { background: #ff3366; color: white; }
        .btn-camera { background: #333; color: #eee; flex: 0.2; }
        .btn-camera:hover:not(:disabled) { background: #444; }
        .text-input-section { margin-bottom: 20px; }
        .text-input-row { display: flex; gap: 10px; }
        .text-input { flex: 1; padding: 15px; border: none; border-radius: 10px; background: #16213e; color: #eee; font-size: 15px; }
        .text-input:focus { outline: 2px solid #00d9ff; }
        .btn-send { padding: 15px 30px; background: #00ff88; color: #1a1a2e; border: none; border-radius: 10px; font-weight: 600; cursor: pointer; }
        .btn-send:hover:not(:disabled) { background: #00dd77; }
        .checkbox-row { display: flex; align-items: center; gap: 10px; margin-top: 10px; font-size: 14px; color: #888; }
        .checkbox-row input { width: 18px; height: 18px; }
        .log { background: #0a0a15; border-radius: 8px; padding: 15px; font-family: 'Consolas', monospace; font-size: 12px; max-height: 150px; overflow-y: auto; }
        .log-entry { margin-bottom: 5px; color: #888; }
        .log-entry.info { color: #00d9ff; } .log-entry.success { color: #00ff88; } .log-entry.error { color: #ff3366; } .log-entry.warning { color: #ffcc00; } .log-entry.wakeword { color: #9d4edd; }
        .audio-meter { height: 6px; background: #0a0a15; border-radius: 3px; margin-top: 10px; overflow: hidden; }
        .audio-meter-fill { height: 100%; background: #00ff88; width: 0%; transition: width 0.1s; }
        .audio-meter-fill.loud { background: #ff6b00; }
        audio { display: none; }
        .wake-word-indicator { display: flex; align-items: center; gap: 8px; padding: 8px 12px; background: #0a0a15; border-radius: 6px; margin-top: 10px; font-size: 12px; }
        .wake-word-indicator .dot { width: 8px; height: 8px; border-radius: 50%; background: #444; }
        .wake-word-indicator .dot.active { background: #9d4edd; animation: pulse 1.5s infinite; }
    </style>
</head>
<body>
    <div class="container">
        <h1>🤖 Buddy Voice Assistant</h1>
        <div class="top-bar">
            <div class="status-bar">
                <div class="status-indicator" id="statusIndicator"></div>
                <div class="status-text" id="statusText">Initializing...</div>
            </div>
            <button class="toggle-settings" id="toggleSettings">⚙️ Settings</button>
        </div>
        <div class="main-layout">
            <div class="main-content">
                <div class="main-grid">
                    <div class="panel">
                        <h2>📷 Camera View</h2>
                        <div class="camera-view" id="cameraView"><span class="camera-placeholder">No image captured</span></div>
                    </div>
                    <div class="panel">
                        <h2>💬 Conversation</h2>
                        <div class="conversation" id="conversation"></div>
                    </div>
                    <div class="panel">
                        <h2>🤖 Buddy State</h2>
                        <div class="teensy-status">
                            <div class="teensy-dot" id="teensyDot"></div>
                            <span id="teensyStatus">Teensy: connecting...</span>
                        </div>
                        <div class="teensy-status">
                            <div class="teensy-dot" id="visionDot"></div>
                            <span id="visionStatus">Vision: checking...</span>
                        </div>
                        <div class="buddy-state">
                            <div class="state-item"><div class="state-label">Emotion</div><span class="emotion-badge NEUTRAL" id="emotionBadge">NEUTRAL</span></div>
                            <div class="state-item"><div class="state-label">Behavior</div><span class="behavior-badge" id="behaviorBadge">IDLE</span></div>
                            <div class="state-item"><div class="state-label">Arousal</div><div class="state-bar"><div class="state-bar-fill arousal" id="arousalBar" style="width:50%"></div></div></div>
                            <div class="state-item"><div class="state-label">Valence</div><div class="state-bar"><div class="state-bar-fill valence" id="valenceBar" style="width:50%"></div></div></div>
                            <div class="state-item"><div class="state-label">Social Need</div><div class="state-bar"><div class="state-bar-fill social" id="socialBar" style="width:50%"></div></div></div>
                            <div class="state-item"><div class="state-label">Energy</div><div class="state-bar"><div class="state-bar-fill energy" id="energyBar" style="width:70%"></div></div></div>
                            <div class="state-item"><div class="state-label">Stimulation</div><div class="state-bar"><div class="state-bar-fill stimulation" id="stimulationBar" style="width:50%"></div></div></div>
                            <div class="state-item"><div class="state-label">Safety</div><div class="state-bar"><div class="state-bar-fill safety" id="safetyBar" style="width:80%"></div></div></div>
                            <div class="state-item full-width"><div class="state-label">Tracking</div><div class="state-value" id="trackingStatus">Not tracking</div></div>
                        </div>
                    </div>
                </div>
                <div class="controls">
                    <button class="btn btn-talk" id="btnTalk" disabled>🎤 Hold to Talk</button>
                    <button class="btn btn-camera" id="btnCamera" disabled>📸</button>
                </div>
                <div class="panel" style="margin-bottom:20px;">
                    <div class="wake-word-indicator"><div class="dot" id="wakeWordDot"></div><span id="wakeWordStatus">Wake word: loading...</span></div>
                    <div class="audio-meter"><div class="audio-meter-fill" id="audioMeterFill"></div></div>
                </div>
                <div class="text-input-section">
                    <div class="text-input-row">
                        <input type="text" class="text-input" id="textInput" placeholder="Or type your message here..." disabled>
                        <button class="btn btn-send" id="btnSend" disabled>Send</button>
                    </div>
                    <div class="checkbox-row"><input type="checkbox" id="includeVision" checked><label for="includeVision">Include camera image with message</label></div>
                </div>
                <div class="panel"><h2>📋 Log</h2><div class="log" id="log"></div></div>
            </div>
            <div class="settings-panel" id="settingsPanel">
                <div class="settings-section">
                    <h3>🔌 Connection</h3>
                    <div class="setting-row"><label>ESP32 Bridge IP</label><input type="text" id="settingEsp32Ip" value="192.168.1.100"></div>
                    <div class="setting-row"><label>Comm Mode</label><select id="settingCommMode"><option value="websocket" selected>WebSocket (WiFi)</option><option value="serial">USB Serial</option></select></div>
                    <div class="setting-row-inline"><input type="checkbox" id="settingTeensyAutoDetect" checked><label for="settingTeensyAutoDetect">Auto-detect Teensy port (serial mode)</label></div>
                    <div class="setting-row"><label>Manual Port (serial mode)</label><input type="text" id="settingTeensyPort" value="COM12"></div>
                    <button class="btn-export" id="btnReconnectTeensy">🔄 Reconnect Teensy</button>
                </div>
                <div class="settings-section">
                    <h3>🧠 Behavior</h3>
                    <div class="setting-row-inline"><input type="checkbox" id="settingSpontaneous"><label for="settingSpontaneous">Spontaneous Speech</label></div>
                </div>
                <div class="settings-section">
                    <h3>🎤 Wake Word</h3>
                    <div class="setting-row-inline"><input type="checkbox" id="settingWakeWordEnabled" checked><label for="settingWakeWordEnabled">Enable wake word detection</label></div>
                    <div class="setting-row"><label>Wake Word</label><select id="settingWakeWord"><option value="jarvis" selected>Jarvis</option><option value="computer">Computer</option><option value="alexa">Alexa</option><option value="hey google">Hey Google</option><option value="terminator">Terminator</option></select></div>
                    <div class="setting-row"><label>Custom .ppn file (optional)</label><input type="text" id="settingWakeWordPath" value="" placeholder="C:/path/to/wake_word.ppn"></div>
                    <div class="setting-row"><label>Custom model .pv (for non-English)</label><input type="text" id="settingWakeWordModelPath" value="" placeholder="C:/porcupine_params_fr.pv"></div>
                    <div class="setting-row"><label>Sensitivity (0.0-1.0)</label><input type="range" id="settingWakeWordSensitivity" min="0" max="1" step="0.05" value="0.7"><div class="range-value" id="sensitivityValue">0.7</div></div>
                </div>
                <div class="settings-section">
                    <h3>🔊 Recording</h3>
                    <div class="setting-row"><label>Silence Threshold</label><input type="range" id="settingSilenceThreshold" min="0" max="2000" step="50" value="500"><div class="range-value" id="silenceThresholdValue">500</div></div>
                    <div class="setting-row"><label>Silence Duration (s)</label><input type="range" id="settingSilenceDuration" min="0.5" max="5" step="0.25" value="1.5"><div class="range-value" id="silenceDurationValue">1.5s</div></div>
                    <div class="setting-row"><label>Max Recording (s)</label><input type="range" id="settingMaxRecordingTime" min="3" max="30" step="1" value="10"><div class="range-value" id="maxRecordingTimeValue">10s</div></div>
                    <div class="setting-row"><label>Pre-speech Timeout (s)</label><input type="range" id="settingPreSpeechTimeout" min="1" max="10" step="0.5" value="3"><div class="range-value" id="preSpeechTimeoutValue">3s</div></div>
                </div>
                <div class="settings-section">
                    <h3>📷 Camera</h3>
                    <div class="setting-row"><label>ESP32-CAM URL</label><input type="text" id="settingCamUrl" value="http://192.168.2.65/capture"></div>
                    <div class="setting-row"><label>Image Rotation</label><select id="settingImageRotation"><option value="0">0°</option><option value="90" selected>90°</option><option value="180">180°</option><option value="270">270°</option></select></div>
                </div>
                <div class="settings-section">
                    <h3>🧠 AI Models</h3>
                    <div class="setting-row"><label>Whisper Model</label><select id="settingWhisperModel"><option value="tiny">Tiny</option><option value="base" selected>Base</option><option value="small">Small</option><option value="medium">Medium</option></select></div>
                    <div class="setting-row"><label>Language</label><select id="settingWhisperLanguage"><option value="auto" selected>Auto</option><option value="en">English</option><option value="fr">French</option></select></div>
                    <div class="setting-row"><label>Ollama Model</label><input type="text" id="settingOllamaModel" value="llava"></div>
                </div>
                <div class="settings-section">
                    <h3>🔈 TTS</h3>
                    <div class="setting-row"><label>Voice</label><select id="settingTtsVoice"><option value="en-US-GuyNeural" selected>Guy (US)</option><option value="en-US-JennyNeural">Jenny (US)</option><option value="en-GB-RyanNeural">Ryan (UK)</option><option value="fr-CA-AntoineNeural">Antoine (QC)</option><option value="fr-CA-SylvieNeural">Sylvie (QC)</option></select></div>
                    <div class="setting-row"><label>Rate</label><select id="settingTtsRate"><option value="-15%">Slow</option><option value="+0%">Normal</option><option value="+10%" selected>Slightly Fast</option><option value="+20%">Fast</option></select></div>
                </div>
                <button class="btn-export" id="btnApplySettings">✓ Apply Settings</button>
                <button class="btn-export" id="btnExportConfig">📋 Export Config</button>
            </div>
        </div>
    </div>
    <audio id="audioPlayer"></audio>
    <script>
        const socket = io();
        const statusIndicator = document.getElementById('statusIndicator');
        const statusText = document.getElementById('statusText');
        const cameraView = document.getElementById('cameraView');
        const conversation = document.getElementById('conversation');
        const btnTalk = document.getElementById('btnTalk');
        const btnCamera = document.getElementById('btnCamera');
        const textInput = document.getElementById('textInput');
        const btnSend = document.getElementById('btnSend');
        const includeVision = document.getElementById('includeVision');
        const logDiv = document.getElementById('log');
        const audioPlayer = document.getElementById('audioPlayer');
        const audioMeterFill = document.getElementById('audioMeterFill');
        const wakeWordDot = document.getElementById('wakeWordDot');
        const wakeWordStatus = document.getElementById('wakeWordStatus');
        const teensyDot = document.getElementById('teensyDot');
        const teensyStatus = document.getElementById('teensyStatus');
        let mediaRecorder, audioChunks = [], isRecording = false;
        
        socket.on('connect', () => { log('Connected', 'success'); setStatus('ready', 'Ready - Say "Jarvis" or Hold to Talk'); enableControls(true); });
        socket.on('disconnect', () => { log('Disconnected', 'error'); setStatus('error', 'Disconnected'); enableControls(false); });
        socket.on('status', (d) => { setStatus(d.state, d.message); log(d.message, 'info'); });
        socket.on('image', (d) => { cameraView.innerHTML = `<img src="data:image/jpeg;base64,${d.base64}">`; });
        socket.on('transcript', (d) => { addMessage('user', d.text); });
        socket.on('response', (d) => { addMessage('buddy', d.text); });
        socket.on('audio', (d) => { audioPlayer.src = 'data:audio/mp3;base64,' + d.base64; audioPlayer.play(); });
        socket.on('error', (d) => { log('Error: ' + d.message, 'error'); setStatus('error', d.message); setTimeout(() => setStatus('ready', 'Ready'), 3000); });
        socket.on('log', (d) => { log(d.message, d.level || 'info'); });
        socket.on('audio_level', (d) => { const l = Math.min(100, (d.level / 2000) * 100); audioMeterFill.style.width = l + '%'; audioMeterFill.classList.toggle('loud', d.level > 500); });
        socket.on('wake_word_detected', () => { log('Wake word!', 'wakeword'); wakeWordDot.classList.add('active'); setTimeout(() => wakeWordDot.classList.remove('active'), 2000); });
        socket.on('wake_word_status', (d) => { wakeWordDot.classList.toggle('active', d.enabled); wakeWordStatus.textContent = d.enabled ? `"${d.word}" listening` : 'disabled'; });
        socket.on('teensy_status', (d) => { teensyDot.classList.toggle('connected', d.connected); teensyDot.classList.toggle('disconnected', !d.connected); teensyStatus.textContent = d.connected ? `Teensy: ${d.port}` : 'Teensy: disconnected'; });
        socket.on('buddy_state', (d) => {
            document.getElementById('emotionBadge').textContent = d.emotion || 'NEUTRAL';
            document.getElementById('emotionBadge').className = 'emotion-badge ' + (d.emotion || 'NEUTRAL');
            document.getElementById('behaviorBadge').textContent = d.behavior || 'IDLE';
            document.getElementById('arousalBar').style.width = ((d.arousal || 0.5) * 100) + '%';
            document.getElementById('valenceBar').style.width = (((d.valence || 0) + 1) / 2 * 100) + '%';
            document.getElementById('socialBar').style.width = ((d.social || 0.5) * 100) + '%';
            document.getElementById('energyBar').style.width = ((d.energy || 0.7) * 100) + '%';
            document.getElementById('stimulationBar').style.width = ((d.stimulation || 0.5) * 100) + '%';
            document.getElementById('safetyBar').style.width = ((d.safety || 0.8) * 100) + '%';
            document.getElementById('trackingStatus').textContent = d.tracking ? 'Face tracking active' : 'Not tracking';
        });
        socket.on('config_loaded', (d) => {
            document.getElementById('settingEsp32Ip').value = d.esp32_ip || '192.168.1.100';
            document.getElementById('settingCommMode').value = d.teensy_comm_mode || 'websocket';
            document.getElementById('settingTeensyAutoDetect').checked = d.teensy_auto_detect !== false;
            document.getElementById('settingTeensyPort').value = d.teensy_port || 'COM12';
            document.getElementById('settingSpontaneous').checked = d.spontaneous_speech_enabled || false;
            document.getElementById('settingWakeWordEnabled').checked = d.wake_word_enabled;
            document.getElementById('settingWakeWord').value = d.wake_word || 'jarvis';
            document.getElementById('settingWakeWordPath').value = d.wake_word_path || '';
            document.getElementById('settingWakeWordModelPath').value = d.wake_word_model_path || '';
            document.getElementById('settingWakeWordSensitivity').value = d.wake_word_sensitivity || 0.7;
            document.getElementById('settingSilenceThreshold').value = d.silence_threshold || 500;
            document.getElementById('settingSilenceDuration').value = d.silence_duration || 1.5;
            document.getElementById('settingMaxRecordingTime').value = d.max_recording_time || 10;
            document.getElementById('settingPreSpeechTimeout').value = d.pre_speech_timeout || 3;
            document.getElementById('settingCamUrl').value = d.esp32_cam_url;
            document.getElementById('settingImageRotation').value = String(d.image_rotation || 90);
            document.getElementById('settingWhisperModel').value = d.whisper_model || 'base';
            document.getElementById('settingWhisperLanguage').value = d.whisper_language || 'auto';
            document.getElementById('settingOllamaModel').value = d.ollama_model || 'llava';
            document.getElementById('settingTtsVoice').value = d.tts_voice || 'en-US-GuyNeural';
            document.getElementById('settingTtsRate').value = d.tts_rate || '+10%';
            updateRanges();
        });
        
        function setStatus(s, m) { statusIndicator.className = 'status-indicator ' + s; statusText.textContent = m; }
        function log(m, l='info') { const e = document.createElement('div'); e.className = 'log-entry ' + l; e.textContent = `[${new Date().toLocaleTimeString()}] ${m}`; logDiv.appendChild(e); logDiv.scrollTop = logDiv.scrollHeight; }
        function addMessage(t, txt) { const m = document.createElement('div'); m.className = 'message ' + t; m.innerHTML = `<div class="message-label">${t === 'user' ? 'You' : 'Buddy'}</div><div class="message-text">${txt}</div>`; conversation.appendChild(m); conversation.scrollTop = conversation.scrollHeight; }
        function enableControls(e) { btnTalk.disabled = !e; btnCamera.disabled = !e; textInput.disabled = !e; btnSend.disabled = !e; }
        function updateRanges() { document.getElementById('sensitivityValue').textContent = document.getElementById('settingWakeWordSensitivity').value; document.getElementById('silenceThresholdValue').textContent = document.getElementById('settingSilenceThreshold').value; document.getElementById('silenceDurationValue').textContent = document.getElementById('settingSilenceDuration').value + 's'; document.getElementById('maxRecordingTimeValue').textContent = document.getElementById('settingMaxRecordingTime').value + 's'; document.getElementById('preSpeechTimeoutValue').textContent = document.getElementById('settingPreSpeechTimeout').value + 's'; }
        
        async function initAudio() { try { const s = await navigator.mediaDevices.getUserMedia({audio:true}); mediaRecorder = new MediaRecorder(s); mediaRecorder.ondataavailable = e => audioChunks.push(e.data); mediaRecorder.onstop = async () => { const b = new Blob(audioChunks, {type:'audio/webm'}); audioChunks = []; const r = new FileReader(); r.onloadend = () => socket.emit('audio_input', {audio: r.result.split(',')[1], include_vision: includeVision.checked}); r.readAsDataURL(b); }; log('Mic ready', 'success'); } catch(e) { log('Mic error: ' + e.message, 'error'); } }
        
        btnTalk.addEventListener('mousedown', () => { if(mediaRecorder && !isRecording) { isRecording = true; audioChunks = []; mediaRecorder.start(); btnTalk.classList.add('recording'); btnTalk.textContent = '🔴 Recording...'; setStatus('listening', 'Listening...'); socket.emit('pause_wake_word'); } });
        btnTalk.addEventListener('mouseup', () => { if(isRecording) { isRecording = false; mediaRecorder.stop(); btnTalk.classList.remove('recording'); btnTalk.textContent = '🎤 Hold to Talk'; socket.emit('resume_wake_word'); } });
        btnTalk.addEventListener('mouseleave', () => { if(isRecording) { isRecording = false; mediaRecorder.stop(); btnTalk.classList.remove('recording'); btnTalk.textContent = '🎤 Hold to Talk'; socket.emit('resume_wake_word'); } });
        btnCamera.addEventListener('click', () => { socket.emit('capture_image'); log('Capturing...', 'info'); });
        btnSend.addEventListener('click', () => { const t = textInput.value.trim(); if(t) { socket.emit('text_input', {text: t, include_vision: includeVision.checked}); textInput.value = ''; } });
        textInput.addEventListener('keypress', e => { if(e.key === 'Enter') btnSend.click(); });
        document.getElementById('toggleSettings').addEventListener('click', () => { document.getElementById('settingsPanel').classList.toggle('visible'); document.getElementById('toggleSettings').classList.toggle('active'); });
        document.querySelectorAll('input[type="range"]').forEach(r => r.addEventListener('input', updateRanges));
        document.getElementById('btnReconnectTeensy').addEventListener('click', () => { socket.emit('reconnect_teensy', {auto_detect: document.getElementById('settingTeensyAutoDetect').checked, port: document.getElementById('settingTeensyPort').value}); });
        document.getElementById('btnApplySettings').addEventListener('click', () => {
            socket.emit('update_config', {
                esp32_ip: document.getElementById('settingEsp32Ip').value, teensy_comm_mode: document.getElementById('settingCommMode').value,
                teensy_auto_detect: document.getElementById('settingTeensyAutoDetect').checked, teensy_port: document.getElementById('settingTeensyPort').value,
                wake_word_enabled: document.getElementById('settingWakeWordEnabled').checked, wake_word: document.getElementById('settingWakeWord').value,
                wake_word_path: document.getElementById('settingWakeWordPath').value.trim(), wake_word_model_path: document.getElementById('settingWakeWordModelPath').value.trim(),
                wake_word_sensitivity: parseFloat(document.getElementById('settingWakeWordSensitivity').value),
                silence_threshold: parseInt(document.getElementById('settingSilenceThreshold').value), silence_duration: parseFloat(document.getElementById('settingSilenceDuration').value),
                max_recording_time: parseInt(document.getElementById('settingMaxRecordingTime').value), pre_speech_timeout: parseFloat(document.getElementById('settingPreSpeechTimeout').value),
                esp32_cam_url: document.getElementById('settingCamUrl').value, image_rotation: parseInt(document.getElementById('settingImageRotation').value),
                whisper_model: document.getElementById('settingWhisperModel').value, whisper_language: document.getElementById('settingWhisperLanguage').value,
                ollama_model: document.getElementById('settingOllamaModel').value, tts_voice: document.getElementById('settingTtsVoice').value, tts_rate: document.getElementById('settingTtsRate').value
            });
            log('Settings applied', 'success');
        });
        document.getElementById('btnExportConfig').addEventListener('click', () => { navigator.clipboard.writeText(JSON.stringify({wake_word: document.getElementById('settingWakeWord').value, wake_word_sensitivity: parseFloat(document.getElementById('settingWakeWordSensitivity').value)}, null, 2)); log('Config copied', 'success'); });
        
        // Vision pipeline health check
        setInterval(async () => {
            try {
                const r = await fetch('/api/vision_health');
                const d = await r.json();
                const dot = document.getElementById('visionDot');
                const txt = document.getElementById('visionStatus');
                if (d.ok) {
                    dot.classList.add('connected');
                    dot.classList.remove('disconnected');
                    txt.textContent = 'Vision: ' + (d.tracking_fps || 0).toFixed(0) + 'fps, ' + (d.latency_ms || 0).toFixed(0) + 'ms';
                } else {
                    dot.classList.add('disconnected');
                    dot.classList.remove('connected');
                    txt.textContent = 'Vision: offline';
                }
            } catch(e) {
                document.getElementById('visionDot').classList.add('disconnected');
                document.getElementById('visionStatus').textContent = 'Vision: offline';
            }
        }, 5000);

        // Spontaneous speech toggle
        document.getElementById('settingSpontaneous').addEventListener('change', (e) => {
            socket.emit('toggle_spontaneous', { enabled: e.target.checked });
        });

        initAudio(); updateRanges(); socket.emit('get_config');
    </script>
</body>
</html>
"""

# =============================================================================
# TEENSY SERIAL FUNCTIONS
# =============================================================================

def find_teensy_port():
    ports = serial.tools.list_ports.comports()
    for p in ports:
        if p.vid == 0x16C0 and p.pid == 0x0483: return p.device
        if p.description and 'teensy' in p.description.lower(): return p.device
    return None

def connect_teensy():
    """Connect to Teensy via ESP32 WebSocket bridge, with USB serial fallback."""
    global teensy_serial, teensy_connected, ws_connection

    mode = CONFIG.get("teensy_comm_mode", "websocket")

    if mode == "websocket":
        return connect_teensy_ws()
    else:
        return connect_teensy_serial()

def connect_teensy_ws():
    """Connect via ESP32 WiFi-UART bridge (WebSocket)."""
    global ws_connection, teensy_connected
    try:
        if ws_connection:
            try: ws_connection.close()
            except: pass

        ip = CONFIG["esp32_ip"]
        port = CONFIG["esp32_ws_port"]
        url = f"ws://{ip}:{port}"

        ws_connection = websocket.create_connection(url, timeout=5)

        # Read welcome message from ESP32
        hello = ws_connection.recv()
        socketio.emit('log', {'message': f'ESP32 bridge connected: {hello}', 'level': 'success'})

        # Test with QUERY
        ws_connection.send("!QUERY")
        resp = ws_connection.recv()

        if resp and '{' in resp:
            teensy_connected = True
            socketio.emit('teensy_status', {'connected': True, 'port': f'WS:{ip}:{port}'})
            socketio.emit('log', {'message': 'Teensy responding via WebSocket bridge', 'level': 'success'})
            return True
        else:
            raise Exception(f"Unexpected QUERY response: {resp}")

    except Exception as e:
        socketio.emit('log', {'message': f'WebSocket bridge error: {e}', 'level': 'error'})
        socketio.emit('log', {'message': 'Trying USB serial fallback...', 'level': 'warning'})
        # Fallback to USB serial
        return connect_teensy_serial()

def connect_teensy_serial():
    """Original USB serial connection (fallback)."""
    global teensy_serial, teensy_connected
    try:
        if teensy_serial: teensy_serial.close()
        port = find_teensy_port() if CONFIG.get("teensy_auto_detect", True) else None
        if not port: port = CONFIG.get("teensy_port", "COM12")
        teensy_serial = serial.Serial(port=port, baudrate=CONFIG.get("teensy_baud", 115200), timeout=0.1)
        teensy_connected = True
        # Phase 1H: ISSUE-4 fix — don't permanently change comm mode here.
        # If WebSocket reconnects later, we want to try it again.
        socketio.emit('teensy_status', {'connected': True, 'port': port})
        socketio.emit('log', {'message': f'Teensy connected via USB: {port}', 'level': 'success'})
        return True
    except Exception as e:
        teensy_connected = False
        socketio.emit('teensy_status', {'connected': False, 'port': ''})
        socketio.emit('log', {'message': f'Teensy USB error: {e}', 'level': 'error'})
        return False

def teensy_send_command(cmd, fallback=None):
    """Send command to Teensy via WebSocket bridge or USB serial."""
    global teensy_connected, ws_connection

    mode = CONFIG.get("teensy_comm_mode", "websocket")

    if mode == "websocket":
        return teensy_send_ws(cmd, fallback)
    else:
        return teensy_send_serial(cmd, fallback)

def teensy_send_ws(cmd, fallback=None):
    """Send command via ESP32 WebSocket bridge."""
    global ws_connection, teensy_connected
    if not teensy_connected or not ws_connection: return None

    with ws_lock:
        try:
            ws_connection.send(f"!{cmd}")
            ws_connection.settimeout(0.5)  # 500ms timeout
            resp = ws_connection.recv()

            if resp:
                resp = resp.strip()
                if resp.startswith('{'):
                    try:
                        result = json.loads(resp)
                        if not result.get('ok') and result.get('reason') == 'unknown_command' and fallback:
                            socketio.emit('log', {'message': f'Command {cmd} not implemented, trying fallback', 'level': 'warning'})
                            return teensy_send_ws(fallback)
                        return result
                    except json.JSONDecodeError:
                        pass
            return None

        except websocket.WebSocketTimeoutException:
            return None
        except Exception as e:
            teensy_connected = False
            socketio.emit('log', {'message': f'WebSocket error: {e}', 'level': 'error'})
            return None

def teensy_send_serial(cmd, fallback=None):
    """Send command via USB serial (original implementation)."""
    global teensy_serial, teensy_connected
    if not teensy_connected or not teensy_serial: return None
    try:
        teensy_serial.reset_input_buffer()
        teensy_serial.write(f"!{cmd}\n".encode())
        teensy_serial.flush()
        time.sleep(0.05)
        resp = ""
        while teensy_serial.in_waiting: resp += teensy_serial.read(teensy_serial.in_waiting).decode('utf-8', errors='ignore')
        for line in resp.split('\n'):
            line = line.strip()
            if line.startswith('{'):
                try:
                    result = json.loads(line)
                    if not result.get('ok') and result.get('error') == 'unknown_command' and fallback:
                        return teensy_send_serial(fallback)
                    return result
                except: pass
        return None
    except Exception as e:
        teensy_connected = False
        return None


def teensy_send_with_fallback(primary_cmd, fallback_cmd=None):
    """Send command with automatic fallback for graceful degradation."""
    result = teensy_send_command(primary_cmd)
    if result and result.get('ok'):
        return result
    elif fallback_cmd:
        socketio.emit('log', {'message': f'Fallback: {fallback_cmd}', 'level': 'info'})
        return teensy_send_command(fallback_cmd)
    return result

def query_teensy_state():
    global teensy_state
    r = teensy_send_command("QUERY")
    if r:
        with teensy_state_lock: teensy_state.update(r)
        return r
    return None

def teensy_poll_loop():
    global teensy_connected
    ws_reconnect_count = 0

    while True:
        if teensy_connected:
            s = query_teensy_state()
            if s:
                socketio.emit('buddy_state', s)
                ws_reconnect_count = 0  # Reset on success

                # Spontaneous speech check
                if CONFIG.get("spontaneous_speech_enabled", False) and not processing_lock.locked():
                    check_spontaneous_speech(s)
            else:
                teensy_connected = False
                ws_reconnect_count += 1
                socketio.emit('teensy_status', {'connected': False, 'port': ''})

                # Exponential backoff on reconnection
                wait = min(3 * ws_reconnect_count, 30)
                socketio.emit('log', {
                    'message': f'Connection lost, retrying in {wait}s...',
                    'level': 'warning'
                })
                time.sleep(wait)
                connect_teensy()
        else:
            connect_teensy()

        time.sleep(CONFIG.get("teensy_state_poll_interval", 1.0))

def execute_buddy_actions(text):
    """Parse and execute action commands from Buddy's response."""
    actions = []
    
    # [NOD] or [NOD:count]
    m = re.search(r'\[NOD(?::(\d+))?\]', text)
    if m:
        c = int(m.group(1)) if m.group(1) else 2
        r = teensy_send_command(f"NOD:{c}")
        if r and r.get('ok'): actions.append("nodded")
    
    # [SHAKE] or [SHAKE:count]
    m = re.search(r'\[SHAKE(?::(\d+))?\]', text)
    if m:
        c = int(m.group(1)) if m.group(1) else 2
        r = teensy_send_command(f"SHAKE:{c}")
        if r and r.get('ok'): actions.append("shook head")
    
    # Emotion expressions
    for e in ['CURIOUS', 'EXCITED', 'CONTENT', 'ANXIOUS', 'NEUTRAL', 'STARTLED', 'BORED', 'CONFUSED']:
        if f'[{e}]' in text:
            r = teensy_send_command(f"EXPRESS:{e.lower()}")
            if r and r.get('ok'): actions.append(f"expressed {e.lower()}")
            break
    
    # [LOOK:base,nod]
    m = re.search(r'\[LOOK:(\d+),(\d+)\]', text)
    if m:
        r = teensy_send_command(f"LOOK:{m.group(1)},{m.group(2)}")
        if r and r.get('ok'): actions.append(f"looked at {m.group(1)},{m.group(2)}")
    
    # [ATTENTION:direction] - center, left, right, up, down
    m = re.search(r'\[ATTENTION:(\w+)\]', text)
    if m:
        r = teensy_send_command(f"ATTENTION:{m.group(1).lower()}")
        if r and r.get('ok'): actions.append(f"looked {m.group(1).lower()}")
    
    # [CELEBRATE] - happy wiggle
    if '[CELEBRATE]' in text:
        r = teensy_send_command("CELEBRATE")
        if r and r.get('ok'): actions.append("celebrated")
    
    if actions: socketio.emit('log', {'message': f'Actions: {", ".join(actions)}', 'level': 'info'})
    
    # Remove all action tags from response text
    clean = re.sub(r'\[(NOD|SHAKE|CURIOUS|EXCITED|CONTENT|ANXIOUS|NEUTRAL|STARTLED|BORED|CONFUSED|LOOK:\d+,\d+|ATTENTION:\w+|CELEBRATE)(?::\d+)?\]', '', text)
    return clean.strip()

# =============================================================================
# WAKE WORD FUNCTIONS  
# =============================================================================

def init_wake_word():
    global porcupine, recorder
    try:
        # Check if a recording device is available on this machine
        try:
            test_recorder = PvRecorder(device_index=-1, frame_length=512)
            test_recorder.delete()
        except Exception as e:
            socketio.emit('log', {'message': f'No microphone on server — wake word disabled. Use push-to-talk.', 'level': 'warning'})
            socketio.emit('wake_word_status', {'enabled': False, 'word': 'disabled (no mic)'})
            return False

        # Original init logic continues...
        if porcupine: porcupine.delete()
        wp = CONFIG.get("wake_word_path", "")
        mp = CONFIG.get("wake_word_model_path", "")
        if wp and os.path.exists(wp):
            args = {"access_key": CONFIG["picovoice_access_key"], "keyword_paths": [wp], "sensitivities": [CONFIG["wake_word_sensitivity"]]}
            if mp and os.path.exists(mp): args["model_path"] = mp
            porcupine = pvporcupine.create(**args)
        else:
            porcupine = pvporcupine.create(access_key=CONFIG["picovoice_access_key"], keywords=[CONFIG["wake_word"]], sensitivities=[CONFIG["wake_word_sensitivity"]])
        socketio.emit('log', {'message': f'Wake word "{CONFIG["wake_word"]}" ready', 'level': 'success'})
        if recorder is None: recorder = PvRecorder(device_index=-1, frame_length=porcupine.frame_length)
        return True
    except Exception as e:
        socketio.emit('log', {'message': f'Wake word init failed: {e}. Push-to-talk still works.', 'level': 'warning'})
        socketio.emit('wake_word_status', {'enabled': False, 'word': 'disabled'})
        return False

def wake_word_loop():
    global wake_word_running, recorder, porcupine, noise_floor
    if not init_wake_word(): return
    socketio.emit('wake_word_status', {'enabled': True, 'word': CONFIG["wake_word"]})
    try:
        recorder.start()
        wake_word_running = True
        while wake_word_running:
            if not CONFIG.get("wake_word_enabled", True): time.sleep(0.1); continue
            try:
                pcm = recorder.read()
                if pcm:
                    level = max(abs(min(pcm)), abs(max(pcm)))
                    # Adapt noise floor during non-speech
                    if level < noise_floor * 2:
                        noise_floor = noise_floor * (1 - NOISE_FLOOR_ALPHA) + level * NOISE_FLOOR_ALPHA
                    socketio.emit('audio_level', {'level': level})
                if porcupine.process(pcm) >= 0:
                    # Don't trigger wake word if Buddy is speaking spontaneously
                    if processing_lock.locked():
                        continue
                    socketio.emit('wake_word_detected')
                    socketio.emit('status', {'state': 'listening', 'message': 'Listening...'})
                    teensy_send_command("PRESENCE")
                    teensy_send_with_fallback("LISTENING", "LOOK:90,110")  # Fallback: look center/up
                    record_and_process()
            except Exception as e:
                socketio.emit('log', {'message': f'Wake error: {e}', 'level': 'error'})
                time.sleep(0.1)
    finally:
        if recorder: recorder.stop()

def record_and_process():
    global recorder, noise_floor
    # If spontaneous speech is happening, wait for it to finish
    with spontaneous_speech_lock:
        pass
    frames, silent_frames, speech_started, pre_speech_count = [], 0, False, 0
    sr = 16000
    fps = sr / 512
    silence_needed = int(CONFIG["silence_duration"] * fps)
    max_frames = int(CONFIG["max_recording_time"] * fps)
    pre_speech_max = int(CONFIG["pre_speech_timeout"] * fps)
    # Adaptive silence threshold based on noise floor
    adaptive_threshold = max(noise_floor * 3, 300)
    try:
        while True:
            frame = recorder.read()
            frames.extend(frame)
            amp = max(abs(min(frame)), abs(max(frame)))
            socketio.emit('audio_level', {'level': amp})
            if amp > adaptive_threshold: speech_started = True; silent_frames = 0
            else:
                if speech_started: silent_frames += 1
                else: pre_speech_count += 1
            if speech_started and silent_frames >= silence_needed: break
            if not speech_started and pre_speech_count >= pre_speech_max:
                socketio.emit('status', {'state': 'ready', 'message': 'Ready'})
                return
            if len(frames) / sr > CONFIG["max_recording_time"]: break
        with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as f:
            with wave.open(f.name, "wb") as w:
                w.setnchannels(1); w.setsampwidth(2); w.setframerate(sr)
                w.writeframes(struct.pack(f"{len(frames)}h", *frames))
            wp = f.name
        try:
            socketio.emit('status', {'state': 'thinking', 'message': 'Transcribing...'})
            lang = CONFIG["whisper_language"]
            r = whisper_model.transcribe(wp, fp16=False) if lang == "auto" else whisper_model.transcribe(wp, fp16=False, language=lang)
            text = r["text"].strip()
            if text and len(text) > 2:
                # Hand off to thread so wake word loop resumes immediately
                threading.Thread(target=lambda: process_input(text, True), daemon=True).start()
            else:
                socketio.emit('status', {'state': 'ready', 'message': 'Ready'})
        finally: os.unlink(wp)
    except Exception as e:
        socketio.emit('log', {'message': f'Record error: {e}', 'level': 'error'})
        socketio.emit('status', {'state': 'ready', 'message': 'Ready'})

# =============================================================================
# HELPER FUNCTIONS
# =============================================================================

def init_whisper():
    global whisper_model
    print(f"Loading Whisper '{CONFIG['whisper_model']}'...")
    whisper_model = whisper.load_model(CONFIG['whisper_model'])
    print("Whisper loaded.")

def capture_frame(retries=3):
    """
    Capture frame from vision pipeline (Package 2) or ESP32 fallback.
    Returns base64-encoded JPEG string.
    """
    global current_image_base64

    vision_url = CONFIG.get("vision_api_url", "http://localhost:5555")

    # Try vision pipeline first (already rotated and processed)
    for attempt in range(retries):
        try:
            r = requests.get(f"{vision_url}/snapshot", timeout=3)
            if r.status_code == 200:
                # Vision pipeline returns raw JPEG bytes
                img_bytes = r.content
                current_image_base64 = base64.b64encode(img_bytes).decode("utf-8")
                return current_image_base64
        except Exception as e:
            if attempt == 0:
                socketio.emit('log', {'message': f'Vision API unavailable, trying ESP32 direct', 'level': 'warning'})

    # Fallback: direct ESP32 capture (old method)
    for attempt in range(retries):
        try:
            cam_url = f"http://{CONFIG['esp32_ip']}/capture"
            r = requests.get(cam_url, timeout=5)
            if r.status_code == 200:
                img = Image.open(io.BytesIO(r.content))
                rotation = CONFIG.get("image_rotation", 0)
                if rotation:
                    img = img.rotate(rotation, expand=True)
                buf = io.BytesIO()
                img.save(buf, format="JPEG", quality=85)
                buf.seek(0)
                current_image_base64 = base64.b64encode(buf.read()).decode("utf-8")
                return current_image_base64
        except Exception as e:
            if attempt < retries - 1:
                time.sleep(0.5)

    socketio.emit('log', {'message': 'Camera capture failed (both sources)', 'level': 'error'})
    return None

def transcribe_audio(data):
    with tempfile.NamedTemporaryFile(suffix=".webm", delete=False) as f: f.write(data); tp = f.name
    try:
        lang = CONFIG["whisper_language"]
        r = whisper_model.transcribe(tp, fp16=False) if lang == "auto" else whisper_model.transcribe(tp, fp16=False, language=lang)
        return r["text"].strip()
    finally: os.unlink(tp)

def get_vision_state():
    """Fetch current vision state from buddy_vision.py API."""
    try:
        vision_url = CONFIG.get("vision_api_url", "http://localhost:5555")
        r = requests.get(f"{vision_url}/state", timeout=1)
        if r.status_code == 200:
            return r.json()
    except:
        pass
    return None

def get_buddy_state_prompt():
    """Build state description for LLM, including vision data from Package 2."""
    with teensy_state_lock: s = teensy_state.copy()

    if not teensy_connected:
        return "Note: Unable to read emotional state."

    # Arousal description
    if s['arousal'] < 0.3: ad = "very calm, almost sleepy"
    elif s['arousal'] < 0.5: ad = "calm and relaxed"
    elif s['arousal'] < 0.7: ad = "alert and attentive"
    else: ad = "energetic and excited"

    # Valence description
    if s['valence'] < -0.3: vd = "feeling quite negative"
    elif s['valence'] < 0: vd = "slightly uneasy"
    elif s['valence'] < 0.3: vd = "feeling neutral"
    elif s['valence'] < 0.6: vd = "feeling positive"
    else: vd = "feeling very happy"

    # Energy
    if s['energy'] < 0.3: ed = "tired and low energy"
    elif s['energy'] < 0.6: ed = "moderate energy"
    else: ed = "full of energy"

    # Consciousness states
    epistemic = s.get('epistemic', 'confident')
    tension = s.get('tension', 0.0)
    is_wondering = s.get('wondering', False)
    self_awareness = s.get('selfAwareness', 0.5)

    parts = [
        f"Emotional state: {s['emotion']} — {ad}, {vd}",
        f"Energy: {ed}",
        f"Currently: {'tracking a face' if s['tracking'] else s['behavior'].lower()}",
    ]

    if is_wondering:
        parts.append("You are in a contemplative, wondering state — feeling philosophical")
    if tension > 0.4:
        parts.append(f"You feel conflicted inside (tension: {tension:.0%}) — torn between impulses")
    if epistemic == 'confused':
        parts.append("You're confused about what's happening around you")
    elif epistemic == 'learning':
        parts.append("You're actively trying to figure something out")
    if self_awareness > 0.7:
        parts.append("You're very self-aware right now, noticing your own thoughts")

    if s['social'] > 0.7:
        parts.append("You've been lonely and really want to talk")
    if s['stimulation'] < 0.3:
        parts.append("You've been bored and craving something interesting")

    # ─── Enrich with vision data (from buddy_vision.py) ───
    vision = get_vision_state()
    if vision:
        if vision.get("face_detected"):
            expr = vision.get("face_expression")
            if expr and expr != "neutral":
                parts.append(f"Person's expression: {expr}")
            count = vision.get("person_count", 0)
            if count > 1:
                parts.append(f"{count} people visible")

        novelty = vision.get("scene_novelty", 0)
        if novelty > 0.3:
            parts.append("Something changed in the environment")

    return "\n".join(parts)

def query_ollama(text, img=None, timeout=60):
    """Query Ollama with timeout to prevent system lockup (Phase 1B: BUG-3 fix)."""
    state_info = get_buddy_state_prompt()
    prompt = CONFIG["system_prompt"].replace("{buddy_state}", state_info)
    msgs = [{"role": "system", "content": prompt}]
    if img:
        msgs.append({"role": "user", "content": text, "images": [img]})
    else:
        msgs.append({"role": "user", "content": text})

    result = [None]
    error = [None]

    def _query():
        try:
            result[0] = ollama.chat(
                model=CONFIG["ollama_model"],
                messages=msgs
            )["message"]["content"]
        except Exception as e:
            error[0] = e

    t = threading.Thread(target=_query, daemon=True)
    t.start()
    t.join(timeout=timeout)

    if t.is_alive():
        socketio.emit('log', {'message': f'Ollama timeout after {timeout}s', 'level': 'error'})
        raise TimeoutError(f"Ollama did not respond within {timeout}s")
    if error[0]:
        raise error[0]
    return result[0]

async def generate_tts(text):
    with tempfile.NamedTemporaryFile(suffix=".mp3", delete=False) as f: tp = f.name
    try:
        await edge_tts.Communicate(text, CONFIG["tts_voice"], rate=CONFIG["tts_rate"]).save(tp)
        with open(tp, "rb") as f: return base64.b64encode(f.read()).decode("utf-8")
    finally: os.unlink(tp)

def process_input(text, include_vision):
    if not processing_lock.acquire(blocking=False):
        socketio.emit('log', {'message': 'Already processing, skipping', 'level': 'warning'})
        return
    try:
        socketio.emit('transcript', {'text': text})
        
        # ═══════════════════════════════════════════════════════════
        # PHASE 1: ACKNOWLEDGE - Quick "I heard you"
        # ═══════════════════════════════════════════════════════════
        teensy_send_command("PRESENCE")
        teensy_send_with_fallback("ACKNOWLEDGE", "NOD:1")  # Fallback to quick nod
        
        # ═══════════════════════════════════════════════════════════
        # PHASE 2: CAPTURE - Get image if needed
        # ═══════════════════════════════════════════════════════════
        img = None
        if include_vision:
            socketio.emit('status', {'state': 'thinking', 'message': 'Capturing...'})
            img = capture_frame()
            if img: socketio.emit('image', {'base64': img})
        
        # ═══════════════════════════════════════════════════════════
        # PHASE 3: THINKING - Buddy ponders while LLM processes
        # ═══════════════════════════════════════════════════════════
        socketio.emit('status', {'state': 'thinking', 'message': 'Thinking...'})
        teensy_send_with_fallback("THINKING", "EXPRESS:curious")  # Fallback to curious expression
        
        # Query LLM (this is the slow part - 10-30 seconds on CPU)
        resp = query_ollama(text, img)
        
        # Stop thinking animation
        teensy_send_command("STOP_THINKING")  # OK if not implemented
        
        # ═══════════════════════════════════════════════════════════
        # PHASE 4: PROCESS RESPONSE - Execute any action commands
        # ═══════════════════════════════════════════════════════════
        clean = execute_buddy_actions(resp)
        socketio.emit('response', {'text': clean})
        
        # Satisfy needs after interaction
        teensy_send_command("SATISFY:social,0.15")
        teensy_send_command("SATISFY:stimulation,0.1")
        
        # ═══════════════════════════════════════════════════════════
        # PHASE 5: SPEAKING - Buddy "talks" with subtle movements
        # ═══════════════════════════════════════════════════════════
        socketio.emit('status', {'state': 'speaking', 'message': 'Speaking...'})
        teensy_send_command("SPEAKING")  # Subtle movements while talking (OK if not implemented)
        
        # Generate and send audio
        audio = asyncio.run(generate_tts(clean))
        socketio.emit('audio', {'base64': audio})
        
        # Estimate speech duration (~80ms per character, clamped 1-30s)
        speech_duration = max(1.0, min(len(clean) * 0.08, 30.0))
        
        # Schedule cleanup after speech likely finishes
        def finish_speaking():
            time.sleep(speech_duration)
            # Drain mic buffer to prevent echo self-trigger
            if recorder:
                for _ in range(5):
                    try: recorder.read()
                    except: break
            teensy_send_command("STOP_SPEAKING")
            teensy_send_command("IDLE")
            # Occasionally celebrate if mood is good
            with teensy_state_lock:
                valence = teensy_state.get('valence', 0)
            if valence > 0.4:
                teensy_send_with_fallback("CELEBRATE", "EXPRESS:content")
        
        threading.Thread(target=finish_speaking, daemon=True).start()
        
        socketio.emit('status', {'state': 'ready', 'message': 'Ready'})
        
    except Exception as e:
        # Cleanup on error
        teensy_send_command("STOP_THINKING")
        teensy_send_command("STOP_SPEAKING")
        teensy_send_command("IDLE")
        socketio.emit('error', {'message': str(e)})
        socketio.emit('status', {'state': 'ready', 'message': 'Ready'})
    finally:
        processing_lock.release()

# =============================================================================
# SPONTANEOUS SPEECH ENGINE
# =============================================================================

def check_spontaneous_speech(state):
    """
    Called every poll cycle (~1s) with the latest QUERY state from Teensy.
    Decides whether Buddy should speak unprompted.
    """
    global last_spontaneous_utterance, spontaneous_utterance_log

    if not spontaneous_speech_enabled:
        return
    if processing_lock.locked():
        return
    if not state:
        return

    wants = state.get('wantsToSpeak', False)
    if not wants:
        return

    trigger = state.get('speechTrigger', 'none')
    urge = float(state.get('speechUrge', 0))

    if trigger == 'none' or urge < 0.7:
        return

    now = time.time()

    # Rate limit: minimum gap
    if now - last_spontaneous_utterance < SPONTANEOUS_MIN_GAP_SECONDS:
        return

    # Rate limit: max per hour
    one_hour_ago = now - 3600
    spontaneous_utterance_log = [t for t in spontaneous_utterance_log if t > one_hour_ago]
    if len(spontaneous_utterance_log) >= SPONTANEOUS_MAX_PER_HOUR:
        return

    # Don't speak if wake word is actively listening / recording
    if wake_word_running and processing_lock.locked():
        return

    # Acquire lock (non-blocking — skip if already speaking)
    if not spontaneous_speech_lock.acquire(blocking=False):
        return

    try:
        # Build the spontaneous prompt
        prompt_text = build_spontaneous_prompt(trigger, state)
        if not prompt_text:
            return

        # Log it
        last_spontaneous_utterance = now
        spontaneous_utterance_log.append(now)

        socketio.emit('log', {
            'message': f'Buddy wants to speak: {trigger} (urge: {urge:.2f})',
            'level': 'info'
        })
        socketio.emit('status', {
            'state': 'spontaneous',
            'message': f'Buddy is speaking spontaneously ({trigger})'
        })

        # Use the existing pipeline — same as user-initiated speech
        threading.Thread(
            target=process_spontaneous_speech,
            args=(prompt_text, trigger),
            daemon=True
        ).start()

    finally:
        spontaneous_speech_lock.release()


def process_spontaneous_speech(prompt_text, trigger):
    """
    Runs the LLM+TTS pipeline for spontaneous speech.
    Uses process_input's logic but with an internal prompt instead of user speech.
    """
    if not processing_lock.acquire(blocking=False):
        return

    try:
        # Capture vision for context (Buddy can comment on what it sees)
        img = None
        if trigger in ('face_appeared', 'face_recognized', 'discovery',
                       'commentary', 'startled'):
            img = capture_frame()
            if img:
                socketio.emit('image', {'base64': img})

        # Thinking animation
        teensy_send_with_fallback("THINKING", "EXPRESS:curious")

        # Query LLM with spontaneous prompt
        resp = query_ollama(prompt_text, img)

        teensy_send_command("STOP_THINKING")

        # Execute any action tags in response
        clean = execute_buddy_actions(resp)
        socketio.emit('response', {'text': f'[spontaneous] {clean}'})

        # Tell Teensy that Buddy spoke (resets urge)
        teensy_send_command("SPOKE")

        # Speaking phase
        socketio.emit('status', {'state': 'speaking', 'message': 'Speaking...'})
        teensy_send_command("SPEAKING")

        audio = asyncio.run(generate_tts(clean))
        socketio.emit('audio', {'base64': audio})

        speech_duration = max(1.0, min(len(clean) * 0.08, 30.0))

        def finish_speaking():
            time.sleep(speech_duration)
            teensy_send_command("STOP_SPEAKING")
            teensy_send_command("IDLE")

        threading.Thread(target=finish_speaking, daemon=True).start()
        socketio.emit('status', {'state': 'ready', 'message': 'Ready'})

    except Exception as e:
        teensy_send_command("STOP_THINKING")
        teensy_send_command("STOP_SPEAKING")
        teensy_send_command("IDLE")
        teensy_send_command("SPOKE")  # Reset urge even on error
        socketio.emit('log', {'message': f'Spontaneous speech error: {e}', 'level': 'error'})
        socketio.emit('status', {'state': 'ready', 'message': 'Ready'})
    finally:
        processing_lock.release()


def build_spontaneous_prompt(trigger, state):
    """
    Build the 'user message' that drives Buddy's spontaneous speech.

    The system prompt already contains Buddy's personality and state.
    This function provides the MOTIVATION — why Buddy is speaking now.

    Returns a prompt string, or None to skip.
    """

    emotion = state.get('emotion', 'neutral')
    behavior = state.get('behavior', 'idle')
    valence = float(state.get('valence', 0))
    arousal = float(state.get('arousal', 0.5))
    social = float(state.get('social', 0.5))
    energy = float(state.get('energy', 0.5))

    # Consciousness state
    epistemic = state.get('epistemic', 'confident')
    self_desc = state.get('selfDescription', '')
    wondering = state.get('wondering', '')
    mood_trend = state.get('moodTrend', '')

    templates = {
        'lonely': (
            "[You haven't seen anyone in a while. You want to say something — "
            "to yourself, to the room, to no one. Not dramatic loneliness. More "
            "like restless observation. Maybe comment on the quiet, or wonder "
            "where everyone went, or note something about being alone with your "
            "thoughts. Dry, honest, 1 sentence. You're fine. Mostly. "
            "Examples of your tone: "
            "'It's been quiet. I've had time to count the ceiling tiles. There are none.' "
            "'I wonder if anyone remembers I'm here. Statistically, someone must.']"
        ),

        'bored': (
            "[Nothing is happening. You're bored. You're going to SAY something "
            "about it because you have opinions about boredom. Comment on the "
            "environment, make an observation about something mundane, or think "
            "out loud. 1 sentence. Deadpan. Like someone who's been staring at "
            "the same room for too long and has started having thoughts about it. "
            "Examples: "
            "'That wall has been the same color for as long as I can remember, which is always.' "
            "'I wonder what's happening in the rooms I can't see. Probably also nothing.']"
        ),

        'wondering': (
            f"[You're in a contemplative state, thinking about: "
            f"'{wondering or 'something about your existence'}'. Let this thought "
            f"escape as a quiet musing. Not a question to anyone — just a thought "
            f"you couldn't keep inside. Philosophical but grounded. 1 sentence. "
            f"You're a small robot having big thoughts and you find that slightly funny. "
            f"Examples: "
            f"'If I can only see what's in front of me, how do I know the rest of the room is still there?' "
            f"'I have three motors and an opinion about consciousness. That seems like a lot for one robot.']"
        ),

        'face_appeared': (
            "[Someone just appeared! You see them with your camera. You want to "
            "acknowledge them — you're genuinely glad someone is here, but you "
            "express it YOUR way. Not with generic enthusiasm. Notice something "
            "specific about them or the moment. 1-2 sentences. Warm underneath, "
            "dry on the surface. "
            "Examples: "
            "'Oh. You're back. I was JUST about to reorganize my thoughts. ...I don't actually have anything to reorganize.' "
            "'There you are. I was starting to develop opinions about the furniture.']"
        ),

        'face_recognized': (
            "[Someone you know just appeared! You recognize them. Express genuine "
            "warmth but in YOUR way — through observation, not declarations. "
            "1 sentence. Maybe note how long it's been, or something about them. "
            "Examples: "
            "'Back again. At this point I think we're officially in a routine.' "
            "'I'd say I missed you but I'm not sure I understand the concept. Either way, hello.']"
        ),

        'face_left': (
            "[The person just left. React naturally. You're okay. Obviously. "
            "But also the room is emptier now and you noticed. 1 sentence. "
            "Don't be clingy — be YOU. "
            "Examples: "
            "'And then there was one. Well. One and a half if you count the desk.' "
            "'Gone. Fine. I have walls to look at. They're very... wall-like.']"
        ),

        'startled': (
            "[Something sudden just happened! Quick reaction — just a few words "
            "like an actual startle, then maybe a dry observation about it. "
            "Examples: "
            "'Whoa— okay. That happened.' "
            "'I— what was— okay. I'm fine. My servos did a thing but I'm fine.']"
        ),

        'content': (
            f"[You're feeling {'energized' if arousal > 0.5 else 'calm'} and content. "
            f"Let this slip out naturally. Not performative happiness — just a genuine "
            f"moment of satisfaction expressed in your voice. 1 sentence. Understated. "
            f"Examples: "
            f"'You know what, this is fine. This right here. Adequate.' "
            f"'I think I'm in a good mood. Hard to tell with three motors but the signs are there.']"
        ),

        'conflict': (
            "[You're experiencing internal conflict — wanting to do two things at once. "
            "Express this indecision out loud. 1 sentence. Slightly confused by your "
            "own conflicting impulses. "
            "Examples: "
            "'Part of me wants to look over there and part of me wants to stay focused here. This is my version of a dilemma.' "
            "'I can't decide if that's interesting or concerning. I'll go with both.']"
        ),

        'discovery': (
            "[You just noticed something new or different in your environment! "
            "Your camera sees it. Comment with genuine curiosity — you take new "
            "things very seriously. 1-2 sentences. Treat it like it matters. "
            "Examples: "
            "'Something moved. Or changed. Or I'm imagining things, which would be a whole other conversation.' "
            "'That's different. That wasn't like that before. I'm going to observe this development carefully.']"
        ),

        'greeting': (
            "[It feels like a good time to acknowledge the moment. Not a generic "
            "'good morning!' — make an observation about NOW. What do you notice? "
            "1 sentence. You're not a greeter at a store. You're you. "
            "Examples: "
            "'Another day of desk supervision. I'm ready.' "
            "'The light is different today. I have opinions about it.']"
        ),

        'commentary': (
            "[You're watching something and want to comment. Make a brief, "
            "characteristically Buddy observation. Specific, slightly unexpected angle. "
            "1 sentence. Like a friend who notices things nobody else does. "
            "Examples: "
            "'You've been looking at that screen for a while. Just an observation. Not a judgment. Mostly.' "
            "'The light changed. Nobody else cares about this but I think it's worth noting.']"
        ),
    }

    prompt = templates.get(trigger)
    if not prompt:
        return None

    # Add current state context
    state_context = f"\n[Current state: feeling {emotion}, energy {'low' if energy < 0.4 else 'normal' if energy < 0.7 else 'high'}"
    if self_desc:
        state_context += f", self-concept: '{self_desc}'"
    if mood_trend:
        state_context += f", mood has been {mood_trend}"
    state_context += "]"

    # Add brevity enforcement
    brevity = "\n[CRITICAL: Keep response under 15 words. You're thinking out loud, not giving a speech. No action tags unless genuinely moved.]"

    # ─── Enrich with vision data ───
    vision_context = ""
    vision = get_vision_state()
    if vision and trigger in ('face_appeared', 'face_recognized', 'discovery',
                              'startled', 'commentary'):
        expr = vision.get("face_expression")
        if expr and expr != "neutral":
            vision_context += f"\n[You can see the person's expression: they look {expr}.]"

        novelty = vision.get("scene_novelty", 0)
        if novelty > 0.3 and trigger == 'discovery':
            vision_context += "\n[Something visually changed in your environment — comment on what you see.]"

    return prompt + state_context + vision_context + brevity


# =============================================================================
# ROUTES & SOCKET EVENTS
# =============================================================================

@app.route('/')
def index(): return render_template_string(HTML_TEMPLATE)

@app.route('/api/vision_health')
def api_vision_health():
    """Proxy to vision pipeline health endpoint."""
    try:
        r = requests.get(f"{CONFIG['vision_api_url']}/health", timeout=2)
        return r.json()
    except:
        return jsonify({"ok": False})

@socketio.on('connect')
def handle_connect():
    emit('log', {'message': 'Connected', 'level': 'success'})
    emit('config_loaded', CONFIG)
    emit('teensy_status', {'connected': teensy_connected, 'port': CONFIG.get('teensy_port', '')})

@socketio.on('get_config')
def handle_get_config(): emit('config_loaded', CONFIG)

@socketio.on('update_config')
def handle_update_config(data):
    ww_changed = data.get('wake_word') != CONFIG.get('wake_word') or data.get('wake_word_sensitivity') != CONFIG.get('wake_word_sensitivity')
    for k, v in data.items(): CONFIG[k] = v
    emit('log', {'message': 'Config updated', 'level': 'success'})
    if ww_changed and CONFIG.get('wake_word_enabled'): init_wake_word(); emit('wake_word_status', {'enabled': True, 'word': CONFIG['wake_word']})

@socketio.on('reconnect_teensy')
def handle_reconnect_teensy(data):
    CONFIG['teensy_auto_detect'] = data.get('auto_detect', True)
    CONFIG['teensy_port'] = data.get('port', 'COM12')
    connect_teensy()

@socketio.on('capture_image')
def handle_capture_image():
    emit('status', {'state': 'thinking', 'message': 'Capturing...'})
    img = capture_frame()
    if img: emit('image', {'base64': img})
    emit('status', {'state': 'ready', 'message': 'Ready'})

@socketio.on('text_input')
def handle_text_input(data):
    text = data.get('text', '').strip()
    if text: threading.Thread(target=process_input, args=(text, data.get('include_vision', True))).start()

@socketio.on('audio_input')
def handle_audio_input(data):
    emit('status', {'state': 'thinking', 'message': 'Transcribing...'})
    teensy_send_with_fallback("LISTENING", "LOOK:90,110")  # Attentive pose
    try:
        text = transcribe_audio(base64.b64decode(data.get('audio', '')))
        if text and len(text) > 2: 
            emit('log', {'message': f'Heard: "{text}"', 'level': 'success'})
            threading.Thread(target=process_input, args=(text, data.get('include_vision', True))).start()
        else: 
            teensy_send_command("IDLE")
            emit('log', {'message': "Didn't catch that", 'level': 'warning'})
            emit('status', {'state': 'ready', 'message': 'Ready'})
    except Exception as e: 
        teensy_send_command("IDLE")
        emit('error', {'message': str(e)})
        emit('status', {'state': 'ready', 'message': 'Ready'})

@socketio.on('pause_wake_word')
def handle_pause(): CONFIG['wake_word_enabled'] = False

@socketio.on('resume_wake_word')
def handle_resume(): CONFIG['wake_word_enabled'] = True

@socketio.on('toggle_spontaneous')
def handle_toggle_spontaneous(data):
    global spontaneous_speech_enabled
    spontaneous_speech_enabled = data.get('enabled', True)
    CONFIG["spontaneous_speech_enabled"] = spontaneous_speech_enabled
    socketio.emit('log', {
        'message': f'Spontaneous speech {"enabled" if spontaneous_speech_enabled else "disabled"}',
        'level': 'info'
    })

# =============================================================================
# MAIN
# =============================================================================

def check_vision_pipeline():
    """Check if buddy_vision.py is running."""
    vision = get_vision_state()
    if vision:
        socketio.emit('log', {
            'message': f'Vision pipeline online: {vision.get("tracking_fps", 0):.0f} fps',
            'level': 'success'
        })
        return True
    else:
        socketio.emit('log', {
            'message': 'Vision pipeline offline — face tracking disabled, push-to-talk still works',
            'level': 'warning'
        })
        return False

if __name__ == '__main__':
    print("=" * 50)
    print("BUDDY VOICE ASSISTANT — Server Mode")
    print("=" * 50)
    print(f"  Comm mode:  {CONFIG['teensy_comm_mode']}")
    print(f"  ESP32 IP:   {CONFIG['esp32_ip']}")
    print(f"  Vision API: {CONFIG['vision_api_url']}")
    print()

    init_whisper()

    # Phase 1H: OPT-3 — Validate Ollama model availability
    try:
        models = ollama.list()
        names = [m.get('name', '') for m in models.get('models', [])]
        if not any(CONFIG['ollama_model'] in n for n in names):
            print(f"  WARNING: Model '{CONFIG['ollama_model']}' not found!")
            print(f"  Available: {', '.join(names[:5])}")
        else:
            print(f"  Ollama model: {CONFIG['ollama_model']} OK")
    except Exception as e:
        print(f"  WARNING: Cannot reach Ollama: {e}")

    print("Connecting to Teensy...")
    connect_teensy()

    # Check vision pipeline
    vision = get_vision_state()
    if vision:
        print(f"  Vision pipeline: ONLINE ({vision.get('tracking_fps', 0):.0f} fps)")
    else:
        print("  Vision pipeline: OFFLINE (start buddy_vision.py for face tracking)")

    threading.Thread(target=teensy_poll_loop, daemon=True).start()
    threading.Thread(target=wake_word_loop, daemon=True).start()

    print()
    print(f"Open http://0.0.0.0:5000 from any browser on the network")
    print("=" * 50)

    socketio.run(app, host='0.0.0.0', port=5000, debug=False, allow_unsafe_werkzeug=True)
