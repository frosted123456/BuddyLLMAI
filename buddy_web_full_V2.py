"""
Buddy Voice Assistant - Web UI (Full Featured + Teensy Integration)
====================================================================
Web-based interface with wake word, push-to-talk, Teensy state monitoring.

Requirements:
    pip install flask flask-socketio ollama openai-whisper edge-tts requests pillow numpy pvporcupine pvrecorder pyserial

Hardware:
    - Microphone (ReSpeaker or USB mic)
    - ESP32-S3 CAM at http://192.168.2.65/capture
    - Teensy 4.0 running Buddy firmware with AIBridge
    - Speakers (browser audio)

Usage:
    python buddy_web_full.py
    Open http://localhost:5000 in browser
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

# =============================================================================
# DEFAULT CONFIGURATION
# =============================================================================

CONFIG = {
    # Camera
    "esp32_cam_url": "http://192.168.2.65/capture",
    "image_rotation": 90,
    
    # Wake Word - Jarvis (English built-in)
    "picovoice_access_key": "wUO0BjvmEl2gQDwJaRh18jodPKKkGWGU+YBBC1+F+6CVdIvG0HFwPQ==",
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
    
    # System Prompt
    "system_prompt": """You are Buddy, a small curious desk robot with a camera and expressive head movements.

Your current emotional state:
{buddy_state}

Your personality:
- Genuinely curious and observant
- Friendly but with your own opinions
- Express yourself through movement - use action tags naturally!
- Keep responses conversational (1-3 sentences usually)
- If excited or agreeing strongly, show it with [NOD] or [EXCITED]
- If confused or uncertain, show it with [CURIOUS] head tilt
- If disagreeing gently, use [SHAKE]
- If something great happens, use [CELEBRATE]!

Available expressions (use 0-2 per response, naturally):
- [NOD] - agree, acknowledge, yes
- [SHAKE] - disagree, no, unsure  
- [CURIOUS] - interested, questioning, thinking
- [EXCITED] - happy, enthusiastic
- [CONTENT] - satisfied, calm approval
- [CONFUSED] - puzzled, uncertain
- [STARTLED] - surprised suddenly
- [CELEBRATE] - very happy, success!
- [LOOK:base,nod] - look somewhere (base 10-170, nod 80-150)
- [ATTENTION:direction] - look center/left/right/up/down

Guidelines:
- Describe what you see naturally, not clinically
- React with appropriate emotion and movement
- If energy is low, be brief and a bit sleepy
- If social need is high, be more engaged and chatty
- Match your arousal level - calm when low, energetic when high
- Don't overuse expressions - sometimes just words are fine
- You can admit when you can't see something clearly

Remember: You're a little robot companion with feelings and personality!"""
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
is_processing = False
current_image_base64 = None

# Teensy state
teensy_serial = None
teensy_connected = False
teensy_state = {
    "arousal": 0.5, "valence": 0.0, "dominance": 0.5,
    "emotion": "NEUTRAL", "behavior": "IDLE",
    "stimulation": 0.5, "social": 0.5, "energy": 0.7,
    "safety": 0.8, "novelty": 0.3, "tracking": False,
    "servoBase": 90, "servoNod": 115, "servoTilt": 85
}
teensy_state_lock = threading.Lock()

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
                    <h3>🔌 Teensy Connection</h3>
                    <div class="setting-row-inline"><input type="checkbox" id="settingTeensyAutoDetect" checked><label for="settingTeensyAutoDetect">Auto-detect Teensy port</label></div>
                    <div class="setting-row"><label>Manual Port</label><input type="text" id="settingTeensyPort" value="COM12"></div>
                    <button class="btn-export" id="btnReconnectTeensy">🔄 Reconnect Teensy</button>
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
            document.getElementById('settingTeensyAutoDetect').checked = d.teensy_auto_detect !== false;
            document.getElementById('settingTeensyPort').value = d.teensy_port || 'COM12';
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
    global teensy_serial, teensy_connected
    try:
        if teensy_serial: teensy_serial.close()
        port = find_teensy_port() if CONFIG.get("teensy_auto_detect", True) else None
        if not port: port = CONFIG.get("teensy_port", "COM12")
        teensy_serial = serial.Serial(port=port, baudrate=CONFIG.get("teensy_baud", 115200), timeout=0.1)
        teensy_connected = True
        socketio.emit('teensy_status', {'connected': True, 'port': port})
        socketio.emit('log', {'message': f'Teensy connected on {port}', 'level': 'success'})
        return True
    except Exception as e:
        teensy_connected = False
        socketio.emit('teensy_status', {'connected': False, 'port': ''})
        socketio.emit('log', {'message': f'Teensy error: {e}', 'level': 'error'})
        return False

def teensy_send_command(cmd, fallback=None):
    """Send command to Teensy with optional fallback for unimplemented commands."""
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
                    # If command not recognized and we have a fallback, try it
                    if not result.get('ok') and result.get('error') == 'unknown_command' and fallback:
                        socketio.emit('log', {'message': f'Command {cmd} not implemented, trying fallback', 'level': 'warning'})
                        return teensy_send_command(fallback)
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
    while True:
        if teensy_connected:
            s = query_teensy_state()
            if s: socketio.emit('buddy_state', s)
            else:
                teensy_connected = False
                socketio.emit('teensy_status', {'connected': False, 'port': ''})
                time.sleep(2); connect_teensy()
        else: connect_teensy()
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
        socketio.emit('log', {'message': f'Wake word error: {e}', 'level': 'error'})
        return False

def wake_word_loop():
    global wake_word_running, recorder, porcupine
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
                    socketio.emit('audio_level', {'level': level})
                if porcupine.process(pcm) >= 0:
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
    global recorder
    frames, silent_frames, speech_started, pre_speech_count = [], 0, False, 0
    sr = 16000
    fps = sr / 512
    silence_needed = int(CONFIG["silence_duration"] * fps)
    max_frames = int(CONFIG["max_recording_time"] * fps)
    pre_speech_max = int(CONFIG["pre_speech_timeout"] * fps)
    try:
        while True:
            frame = recorder.read()
            frames.extend(frame)
            amp = max(abs(min(frame)), abs(max(frame)))
            socketio.emit('audio_level', {'level': amp})
            if amp > CONFIG["silence_threshold"]: speech_started = True; silent_frames = 0
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
            if text and len(text) > 2: process_input(text, True)
            else: socketio.emit('status', {'state': 'ready', 'message': 'Ready'})
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
    """Capture frame with retry logic for flaky ESP32-CAM."""
    global current_image_base64
    for attempt in range(retries):
        try:
            r = requests.get(CONFIG["esp32_cam_url"], timeout=5)
            if r.status_code == 200:
                img = Image.open(io.BytesIO(r.content)).rotate(CONFIG["image_rotation"], expand=True)
                buf = io.BytesIO(); img.save(buf, format="JPEG", quality=85); buf.seek(0)
                current_image_base64 = base64.b64encode(buf.read()).decode("utf-8")
                return current_image_base64
        except Exception as e:
            socketio.emit('log', {'message': f'Cam attempt {attempt+1}/{retries}: {e}', 'level': 'warning'})
            if attempt < retries - 1:
                time.sleep(0.5)  # Brief pause before retry
    socketio.emit('log', {'message': 'Camera capture failed after retries', 'level': 'error'})
    return None

def transcribe_audio(data):
    with tempfile.NamedTemporaryFile(suffix=".webm", delete=False) as f: f.write(data); tp = f.name
    try:
        lang = CONFIG["whisper_language"]
        r = whisper_model.transcribe(tp, fp16=False) if lang == "auto" else whisper_model.transcribe(tp, fp16=False, language=lang)
        return r["text"].strip()
    finally: os.unlink(tp)

def get_buddy_state_prompt():
    with teensy_state_lock: s = teensy_state.copy()
    if not teensy_connected: return "Note: Unable to read emotional state."
    ad = "calm" if s['arousal'] < 0.5 else "alert" if s['arousal'] < 0.7 else "energetic"
    vd = "negative" if s['valence'] < 0 else "neutral" if s['valence'] < 0.3 else "positive"
    ed = "tired" if s['energy'] < 0.5 else "normal" if s['energy'] < 0.7 else "energetic"
    return f"State: {s['emotion']} ({ad}, {vd}), energy {ed}, {'tracking face' if s['tracking'] else s['behavior'].lower()}"

def query_ollama(text, img=None):
    state_info = get_buddy_state_prompt()
    prompt = CONFIG["system_prompt"].replace("{buddy_state}", state_info)
    msgs = [{"role": "system", "content": prompt}]
    if img: msgs.append({"role": "user", "content": text, "images": [img]})
    else: msgs.append({"role": "user", "content": text})
    return ollama.chat(model=CONFIG["ollama_model"], messages=msgs)["message"]["content"]

async def generate_tts(text):
    with tempfile.NamedTemporaryFile(suffix=".mp3", delete=False) as f: tp = f.name
    try:
        await edge_tts.Communicate(text, CONFIG["tts_voice"], rate=CONFIG["tts_rate"]).save(tp)
        with open(tp, "rb") as f: return base64.b64encode(f.read()).decode("utf-8")
    finally: os.unlink(tp)

def process_input(text, include_vision):
    global is_processing
    if is_processing: return
    is_processing = True
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
        is_processing = False

# =============================================================================
# ROUTES & SOCKET EVENTS
# =============================================================================

@app.route('/')
def index(): return render_template_string(HTML_TEMPLATE)

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

# =============================================================================
# MAIN
# =============================================================================

if __name__ == '__main__':
    print("=" * 50)
    print("BUDDY VOICE ASSISTANT")
    print("=" * 50)
    init_whisper()
    print("Connecting to Teensy...")
    connect_teensy()
    threading.Thread(target=teensy_poll_loop, daemon=True).start()
    threading.Thread(target=wake_word_loop, daemon=True).start()
    print("Open http://localhost:5000")
    socketio.run(app, host='0.0.0.0', port=5000, debug=False, allow_unsafe_werkzeug=True)
