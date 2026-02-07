#!/usr/bin/env python3
"""
buddy_vision.py — Buddy Robot Vision Pipeline
===============================================

Receives MJPEG stream from ESP32-S3, runs face detection on the Server PC,
sends face coordinates back to ESP32 via UDP for forwarding to Teensy.

Also provides rich vision data (objects, expressions, scene) via a local
HTTP API for the main Buddy server to consume.

Requirements:
    pip install opencv-python mediapipe numpy requests flask

Optional (for GPU acceleration):
    pip install opencv-contrib-python  (for CUDA support)

Usage:
    python buddy_vision.py --esp32-ip 192.168.1.100
    python buddy_vision.py --esp32-ip 192.168.1.100 --rotate 90
    python buddy_vision.py --esp32-ip 192.168.1.100 --debug

The --rotate flag applies rotation BEFORE detection (use 90 if camera
is mounted sideways, matching the old ESP32 firmware's 90° CCW rotation).
"""

import cv2
import mediapipe as mp
import numpy as np
import socket
import threading
import time
import argparse
import json
import signal
import sys
from collections import deque
from flask import Flask, jsonify

# ════════════════════════════════════════════════════════════════
# CONFIGURATION
# ════════════════════════════════════════════════════════════════

DEFAULT_CONFIG = {
    "esp32_ip": "192.168.1.100",
    "stream_url": "http://{ip}/stream",
    "udp_port": 8888,

    # Coordinate mapping (Teensy expects 240x240)
    "teensy_frame_width": 240,
    "teensy_frame_height": 240,
    "teensy_center_x": 120,
    "teensy_center_y": 120,

    # Camera rotation (0, 90, 180, 270)
    # Set to 90 if camera is mounted sideways (old ESP32 did 90° CCW)
    "rotation": 0,

    # Detection settings
    "detection_confidence": 0.5,
    "tracking_confidence": 0.5,
    "max_faces": 3,

    # Performance
    "target_tracking_fps": 30,    # Face detection rate
    "target_rich_fps": 3,         # Object/expression rate

    # State API
    "api_port": 5555,

    # Debug
    "debug": False,
    "show_preview": False,
}

# ════════════════════════════════════════════════════════════════
# SHARED STATE
# ════════════════════════════════════════════════════════════════

class VisionState:
    """Thread-safe shared state between vision threads and API."""

    def __init__(self):
        self.lock = threading.Lock()

        # Latest frame (for rich analysis thread)
        self.latest_frame = None
        self.frame_timestamp = 0
        self.frame_count = 0

        # Face tracking state
        self.faces = []                # List of detected faces
        self.primary_face = None       # Main face being tracked
        self.face_detected = False
        self.tracking_fps = 0.0

        # Velocity tracking
        self.prev_face_x = 120
        self.prev_face_y = 120
        self.prev_face_time = 0

        # Rich vision state (updated slowly)
        self.objects = []              # Detected objects in scene
        self.face_expression = None    # Expression of primary face
        self.face_landmarks = None     # Landmark points
        self.scene_description = ""    # Brief scene summary
        self.scene_novelty = 0.0       # How different from previous
        self.person_count = 0

        # Health
        self.stream_connected = False
        self.stream_fps = 0.0
        self.detection_latency_ms = 0.0
        self.udp_sent = 0
        self.errors = 0
        self.last_error = ""

        # Sequence counter (monotonic, matches Teensy expectation)
        self.sequence = 0

    def update_frame(self, frame):
        with self.lock:
            self.latest_frame = frame
            self.frame_timestamp = time.time()
            self.frame_count += 1

    def get_frame(self):
        with self.lock:
            return self.latest_frame, self.frame_timestamp

    def update_tracking(self, faces, primary, fps, latency):
        with self.lock:
            self.faces = faces
            self.primary_face = primary
            self.face_detected = primary is not None
            self.tracking_fps = fps
            self.detection_latency_ms = latency

    def get_velocity(self, x, y):
        """Calculate pixel velocity from previous position."""
        with self.lock:
            now = time.time()
            dt = now - self.prev_face_time
            if dt > 0 and dt < 0.2 and self.prev_face_time > 0:
                # Velocity in pixels/second (in 240px coordinate space)
                vx = int((x - self.prev_face_x) / dt)
                vy = int((y - self.prev_face_y) / dt)
            else:
                vx, vy = 0, 0

            self.prev_face_x = x
            self.prev_face_y = y
            self.prev_face_time = now
            return vx, vy

    def next_sequence(self):
        with self.lock:
            self.sequence += 1
            return self.sequence

    def get_state_dict(self):
        """Full state for API endpoint."""
        with self.lock:
            return {
                "face_detected": self.face_detected,
                "primary_face": self.primary_face,
                "face_count": len(self.faces),
                "tracking_fps": round(self.tracking_fps, 1),
                "detection_latency_ms": round(self.detection_latency_ms, 1),
                "stream_connected": self.stream_connected,
                "stream_fps": round(self.stream_fps, 1),
                "frame_count": self.frame_count,
                "udp_sent": self.udp_sent,
                "objects": self.objects,
                "face_expression": self.face_expression,
                "scene_novelty": round(self.scene_novelty, 2),
                "person_count": self.person_count,
                "errors": self.errors,
                "last_error": self.last_error,
            }


state = VisionState()

# ════════════════════════════════════════════════════════════════
# COORDINATE MAPPING
# ════════════════════════════════════════════════════════════════

def map_to_teensy_coords(x, y, w, h, frame_w, frame_h, config):
    """
    Map face coordinates from actual frame size to Teensy's 240x240 space.

    CRITICAL: Teensy expects ALL coordinates in [0, 240].
    It validates this and REJECTS anything outside.

    Args:
        x, y: face center in frame pixels
        w, h: face width/height in frame pixels
        frame_w, frame_h: actual frame dimensions
        config: configuration dict

    Returns:
        (tx, ty, tw, th): coordinates in 240x240 space
    """
    tw_target = config["teensy_frame_width"]   # 240
    th_target = config["teensy_frame_height"]  # 240

    tx = int(x * tw_target / frame_w)
    ty = int(y * th_target / frame_h)
    tw = int(w * tw_target / frame_w)
    th = int(h * th_target / frame_h)

    # Clamp to valid range (Teensy rejects out-of-bounds)
    tx = max(0, min(tx, tw_target))
    ty = max(0, min(ty, th_target))
    tw = max(1, min(tw, tw_target))
    th = max(1, min(th, th_target))

    return tx, ty, tw, th


def calculate_confidence(detection_score, face_w_240):
    """
    Map MediaPipe detection score to Teensy confidence range.

    Teensy thresholds:
        < 25:  REJECTED (not sent to reflex controller)
        25-69: Low confidence (was histogram tracker range)
        70-95: High confidence (was AI detection range)

    MediaPipe scores are 0.0-1.0 and generally much higher than
    the old ESP32 detector, so we map them into the 70-98 range
    for AI-quality detections.
    """
    # MediaPipe confidence -> Teensy confidence
    # 0.5 -> 70, 0.7 -> 80, 0.9 -> 90, 1.0 -> 95
    base_conf = int(50 + detection_score * 48)
    base_conf = max(25, min(base_conf, 98))

    # Slight boost for larger faces (more reliable at close range)
    if face_w_240 > 60:
        base_conf = min(base_conf + 3, 98)

    return base_conf


# ════════════════════════════════════════════════════════════════
# STREAM RECEIVER THREAD
# ════════════════════════════════════════════════════════════════

def stream_receiver_thread(config):
    """
    Receives MJPEG stream from ESP32 and stores latest frame.
    Runs continuously, handles reconnection.
    """
    url = config["stream_url"].format(ip=config["esp32_ip"])

    fps_counter = deque(maxlen=30)

    while True:
        try:
            print(f"[STREAM] Connecting to {url}...")
            cap = cv2.VideoCapture(url)
            cap.set(cv2.CAP_PROP_BUFFERSIZE, 1)  # Phase 1D: BUG-5 fix — minimize internal buffering

            if not cap.isOpened():
                print("[STREAM] Failed to open stream, retrying in 3s...")
                state.stream_connected = False
                time.sleep(3)
                continue

            print("[STREAM] Connected!")
            state.stream_connected = True

            while cap.isOpened():
                ret, frame = cap.read()
                if not ret:
                    print("[STREAM] Frame read failed, reconnecting...")
                    break

                # Apply rotation if camera is mounted sideways
                rotation = config.get("rotation", 0)
                if rotation == 90:
                    frame = cv2.rotate(frame, cv2.ROTATE_90_COUNTERCLOCKWISE)
                elif rotation == 180:
                    frame = cv2.rotate(frame, cv2.ROTATE_180)
                elif rotation == 270:
                    frame = cv2.rotate(frame, cv2.ROTATE_90_CLOCKWISE)

                state.update_frame(frame)

                # FPS tracking
                fps_counter.append(time.time())
                if len(fps_counter) > 1:
                    dt = fps_counter[-1] - fps_counter[0]
                    if dt > 0:
                        state.stream_fps = (len(fps_counter) - 1) / dt

            cap.release()

        except Exception as e:
            print(f"[STREAM] Error: {e}")
            state.stream_connected = False
            state.errors += 1
            state.last_error = str(e)

        time.sleep(2)  # Wait before reconnect


# ════════════════════════════════════════════════════════════════
# FACE DETECTION THREAD (Fast — 30 FPS target)
# ════════════════════════════════════════════════════════════════

def face_tracking_thread(config):
    """
    Runs MediaPipe face detection on latest frame, sends coordinates
    to ESP32 via UDP for Teensy.

    This is the CRITICAL PATH for tracking responsiveness.
    Optimized for minimum latency.
    """
    # Initialize MediaPipe
    mp_face = mp.solutions.face_detection
    face_detector = mp_face.FaceDetection(
        model_selection=0,  # 0 = short range (< 2m), 1 = full range (< 5m)
        min_detection_confidence=config["detection_confidence"]
    )

    # UDP socket for sending face data to ESP32
    udp_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    esp32_addr = (config["esp32_ip"], config["udp_port"])

    fps_counter = deque(maxlen=30)
    frame_interval = 1.0 / config["target_tracking_fps"]
    last_frame_time = 0
    consecutive_no_face = 0

    print(f"[TRACKING] Started — target {config['target_tracking_fps']} FPS")
    print(f"[TRACKING] Sending UDP to {esp32_addr}")

    while True:
        try:
            now = time.time()

            # Rate limit
            elapsed = now - last_frame_time
            if elapsed < frame_interval:
                time.sleep(frame_interval - elapsed)
                continue

            # Get latest frame
            frame, timestamp = state.get_frame()
            if frame is None:
                time.sleep(0.01)
                continue

            # Skip if we already processed this frame
            if timestamp <= last_frame_time:
                time.sleep(0.001)
                continue
            last_frame_time = timestamp

            # -- DETECTION --
            detect_start = time.time()

            # MediaPipe expects RGB
            rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
            results = face_detector.process(rgb)

            detect_ms = (time.time() - detect_start) * 1000

            frame_h, frame_w = frame.shape[:2]

            if results.detections:
                consecutive_no_face = 0

                # Find the largest/closest face (primary tracking target)
                best_detection = None
                best_area = 0

                faces_list = []

                for detection in results.detections:
                    bbox = detection.location_data.relative_bounding_box
                    score = detection.score[0]

                    # Convert relative coordinates to pixel coordinates
                    fx = int((bbox.xmin + bbox.width / 2) * frame_w)
                    fy = int((bbox.ymin + bbox.height / 2) * frame_h)
                    fw = int(bbox.width * frame_w)
                    fh = int(bbox.height * frame_h)

                    area = fw * fh

                    faces_list.append({
                        "x": fx, "y": fy, "w": fw, "h": fh,
                        "score": score, "area": area
                    })

                    if area > best_area:
                        best_area = area
                        best_detection = {
                            "x": fx, "y": fy, "w": fw, "h": fh,
                            "score": score
                        }

                if best_detection:
                    # Map to Teensy coordinate space
                    tx, ty, tw, th = map_to_teensy_coords(
                        best_detection["x"], best_detection["y"],
                        best_detection["w"], best_detection["h"],
                        frame_w, frame_h, config
                    )

                    # Calculate velocity in Teensy coordinate space
                    vx, vy = state.get_velocity(tx, ty)

                    # Calculate confidence
                    conf = calculate_confidence(best_detection["score"], tw)

                    # Get sequence number
                    seq = state.next_sequence()

                    # Build message — EXACT format Teensy expects
                    msg = f"FACE:{tx},{ty},{vx},{vy},{tw},{th},{conf},{seq}"

                    # Send via UDP to ESP32 -> UART -> Teensy
                    udp_sock.sendto(msg.encode(), esp32_addr)
                    state.udp_sent += 1

                    # Update shared state
                    state.update_tracking(faces_list, {
                        "x": tx, "y": ty, "w": tw, "h": th,
                        "vx": vx, "vy": vy,
                        "conf": conf, "score": best_detection["score"],
                        "frame_x": best_detection["x"],
                        "frame_y": best_detection["y"],
                    }, 0, detect_ms)

                    if config["debug"] and seq % 30 == 0:
                        print(f"[TRACKING] {msg}  ({detect_ms:.1f}ms)")

            else:
                # No face detected
                consecutive_no_face += 1
                seq = state.next_sequence()

                msg = f"NO_FACE,{seq}"
                udp_sock.sendto(msg.encode(), esp32_addr)
                state.udp_sent += 1

                state.update_tracking([], None, 0, detect_ms)

                # Reset velocity tracking after sustained face loss
                if consecutive_no_face > 10:
                    state.prev_face_time = 0

                if config["debug"] and consecutive_no_face == 1:
                    print(f"[TRACKING] Face lost ({detect_ms:.1f}ms)")

            # FPS tracking
            fps_counter.append(time.time())
            if len(fps_counter) > 1:
                dt = fps_counter[-1] - fps_counter[0]
                if dt > 0:
                    current_fps = (len(fps_counter) - 1) / dt
                    state.tracking_fps = current_fps

            # Debug preview
            if config.get("show_preview", False):
                debug_frame = frame.copy()
                if results.detections:
                    for det in results.detections:
                        bbox = det.location_data.relative_bounding_box
                        x1 = int(bbox.xmin * frame_w)
                        y1 = int(bbox.ymin * frame_h)
                        x2 = int((bbox.xmin + bbox.width) * frame_w)
                        y2 = int((bbox.ymin + bbox.height) * frame_h)
                        cv2.rectangle(debug_frame, (x1, y1), (x2, y2), (0, 255, 0), 2)
                        cv2.putText(debug_frame, f"{det.score[0]:.2f}",
                                    (x1, y1 - 10), cv2.FONT_HERSHEY_SIMPLEX,
                                    0.6, (0, 255, 0), 2)

                cv2.putText(debug_frame,
                            f"FPS: {state.tracking_fps:.1f} | Det: {detect_ms:.0f}ms | UDP: {state.udp_sent}",
                            (10, 30), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 255), 2)
                cv2.imshow("Buddy Vision", debug_frame)
                if cv2.waitKey(1) & 0xFF == ord('q'):
                    break

        except Exception as e:
            print(f"[TRACKING] Error: {e}")
            state.errors += 1
            state.last_error = str(e)
            time.sleep(0.1)


# ════════════════════════════════════════════════════════════════
# RICH VISION THREAD (Slow — 2-5 FPS)
# ════════════════════════════════════════════════════════════════

def rich_vision_thread(config):
    """
    Slower, more detailed vision analysis for consciousness/spontaneous speech.
    Not on the critical tracking path — runs at 2-5 FPS.

    Provides:
    - Face landmarks and expression estimation
    - Scene change detection (novelty)
    - Person count
    - Object detection (future: YOLO)
    - Phase 2: Sends VISION updates to Teensy for autonomous observation loop
    """
    # MediaPipe face mesh for landmarks/expressions
    mp_face_mesh = mp.solutions.face_mesh
    face_mesh = mp_face_mesh.FaceMesh(
        max_num_faces=config["max_faces"],
        refine_landmarks=True,
        min_detection_confidence=0.5,
        min_tracking_confidence=0.5
    )

    # Phase 2: UDP socket for sending VISION updates to ESP32 → Teensy
    vision_udp_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    esp32_addr = (config["esp32_ip"], config["udp_port"])

    frame_interval = 1.0 / config["target_rich_fps"]
    prev_frame_gray = None

    print(f"[RICH] Started — target {config['target_rich_fps']} FPS")
    print(f"[RICH] Phase 2: Sending VISION updates to {esp32_addr}")

    while True:
        try:
            time.sleep(frame_interval)

            frame, timestamp = state.get_frame()
            if frame is None:
                continue

            frame_h, frame_w = frame.shape[:2]
            rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)

            # -- Face Mesh (landmarks + expression estimation) --
            mesh_results = face_mesh.process(rgb)

            expression = None
            if mesh_results.multi_face_landmarks:
                state.person_count = len(mesh_results.multi_face_landmarks)

                # Simple expression estimation from key landmarks
                landmarks = mesh_results.multi_face_landmarks[0]
                expression = estimate_expression(landmarks, frame_w, frame_h)
                state.face_expression = expression
                state.face_landmarks = True  # Flag that landmarks are available
            else:
                state.person_count = 0
                state.face_expression = None
                state.face_landmarks = None

            # -- Scene Novelty Detection --
            gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
            gray_small = cv2.resize(gray, (160, 120))

            if prev_frame_gray is not None:
                diff = cv2.absdiff(prev_frame_gray, gray_small)
                novelty = float(np.mean(diff)) / 255.0
                # Smooth the novelty signal
                state.scene_novelty = state.scene_novelty * 0.7 + novelty * 0.3

            prev_frame_gray = gray_small

            # ── Phase 2: Send VISION update to Teensy via UDP ──
            # This closes the observation loop: PC sees → Teensy feels
            vision_cmd = json.dumps({
                "f": 1 if state.face_detected else 0,
                "fc": state.person_count,
                "ex": state.face_expression or "neutral",
                "nv": round(state.scene_novelty, 2),
                "ob": len(state.objects),
                "mv": round(state.scene_novelty, 2),  # Use scene diff as movement proxy
            }, separators=(',', ':'))

            try:
                vision_msg = f"!VISION:{vision_cmd}"
                vision_udp_sock.sendto(vision_msg.encode(), esp32_addr)
            except Exception:
                pass  # Non-critical, next update comes in ~300ms

        except Exception as e:
            if config["debug"]:
                print(f"[RICH] Error: {e}")
            state.errors += 1
            time.sleep(1)


def estimate_expression(landmarks, frame_w, frame_h):
    """
    Simple expression estimation from MediaPipe face landmarks.

    Uses key ratios:
    - Mouth openness (landmark 13 vs 14 — upper/lower lip)
    - Eye openness (landmarks 159 vs 145 — upper/lower eyelid)
    - Brow raise (landmarks 66/107 vs bridge)

    Returns: string expression label
    """
    try:
        # Get key landmark positions
        def lm(idx):
            l = landmarks.landmark[idx]
            return (l.x * frame_w, l.y * frame_h)

        # Mouth openness
        upper_lip = lm(13)
        lower_lip = lm(14)
        mouth_open = abs(upper_lip[1] - lower_lip[1])

        # Left eye openness
        upper_eye = lm(159)
        lower_eye = lm(145)
        eye_open = abs(upper_eye[1] - lower_eye[1])

        # Brow height (relative to nose bridge)
        left_brow = lm(66)
        nose_bridge = lm(6)
        brow_height = abs(left_brow[1] - nose_bridge[1])

        # Face height for normalization
        chin = lm(152)
        forehead = lm(10)
        face_height = abs(chin[1] - forehead[1])

        if face_height < 10:
            return "neutral"

        # Normalize
        mouth_ratio = mouth_open / face_height
        eye_ratio = eye_open / face_height
        brow_ratio = brow_height / face_height

        # Simple classification
        if mouth_ratio > 0.08:
            if brow_ratio > 0.22:
                return "surprised"
            return "happy"  # Open mouth + normal brows = smiling
        elif eye_ratio < 0.02:
            return "squinting"  # Eyes nearly closed
        elif brow_ratio > 0.24:
            return "raised_brows"
        elif brow_ratio < 0.15:
            return "frowning"
        else:
            return "neutral"

    except Exception:
        return "neutral"


# ════════════════════════════════════════════════════════════════
# STATE API (for buddy_web_full_V2.py to consume)
# ════════════════════════════════════════════════════════════════

api_app = Flask(__name__)

# Suppress Flask request logging
import logging
log = logging.getLogger('werkzeug')
log.setLevel(logging.WARNING)


@api_app.route('/state')
def get_state():
    """Full vision state as JSON."""
    return jsonify(state.get_state_dict())


@api_app.route('/face')
def get_face():
    """Just face tracking data (lightweight)."""
    with state.lock:
        return jsonify({
            "detected": state.face_detected,
            "face": state.primary_face,
            "expression": state.face_expression,
            "person_count": state.person_count,
        })


@api_app.route('/health')
def get_health():
    """Health check."""
    return jsonify({
        "ok": state.stream_connected,
        "stream_fps": round(state.stream_fps, 1),
        "tracking_fps": round(state.tracking_fps, 1),
        "latency_ms": round(state.detection_latency_ms, 1),
        "udp_sent": state.udp_sent,
        "errors": state.errors,
    })


@api_app.route('/snapshot')
def get_snapshot():
    """Latest frame as JPEG (for LLM vision queries)."""
    frame, _ = state.get_frame()
    if frame is None:
        return "No frame available", 503

    _, buf = cv2.imencode('.jpg', frame, [cv2.IMWRITE_JPEG_QUALITY, 85])
    return buf.tobytes(), 200, {'Content-Type': 'image/jpeg'}


def api_server_thread(config):
    """Run the state API server."""
    port = config["api_port"]
    print(f"[API] Vision state server on port {port}")
    api_app.run(host='0.0.0.0', port=port, threaded=True)


# ════════════════════════════════════════════════════════════════
# STATUS PRINTER
# ════════════════════════════════════════════════════════════════

def status_printer_thread(config):
    """Periodic status output."""
    while True:
        time.sleep(30)
        s = state.get_state_dict()
        face_str = "TRACKING" if s["face_detected"] else "no face"
        print(f"[STATUS] Stream:{s['stream_fps']:.0f}fps | "
              f"Track:{s['tracking_fps']:.0f}fps ({s['detection_latency_ms']:.0f}ms) | "
              f"{face_str} | "
              f"Persons:{s['person_count']} | "
              f"Expression:{s['face_expression']} | "
              f"Novelty:{s['scene_novelty']:.2f} | "
              f"UDP:{s['udp_sent']} | "
              f"Errors:{s['errors']}")


# ════════════════════════════════════════════════════════════════
# MAIN
# ════════════════════════════════════════════════════════════════

def main():
    parser = argparse.ArgumentParser(description="Buddy Robot Vision Pipeline")
    parser.add_argument("--esp32-ip", required=True, help="ESP32-S3 IP address")
    parser.add_argument("--rotate", type=int, default=0, choices=[0, 90, 180, 270],
                        help="Camera rotation (degrees CCW). Use 90 if mounted sideways.")
    parser.add_argument("--debug", action="store_true", help="Enable debug output")
    parser.add_argument("--preview", action="store_true", help="Show OpenCV preview window")
    parser.add_argument("--api-port", type=int, default=5555, help="Vision state API port")
    parser.add_argument("--model", type=int, default=0, choices=[0, 1],
                        help="MediaPipe model: 0=short range (<2m), 1=full range (<5m)")
    args = parser.parse_args()

    config = DEFAULT_CONFIG.copy()
    config["esp32_ip"] = args.esp32_ip
    config["rotation"] = args.rotate
    config["debug"] = args.debug
    config["show_preview"] = args.preview
    config["api_port"] = args.api_port

    print("+" + "=" * 44 + "+")
    print("|  BUDDY VISION PIPELINE v1.0                |")
    print("+" + "=" * 44 + "+")
    print(f"  ESP32:     {config['esp32_ip']}")
    print(f"  Stream:    http://{config['esp32_ip']}/stream")
    print(f"  UDP out:   {config['esp32_ip']}:{config['udp_port']}")
    print(f"  Rotation:  {config['rotation']}deg")
    print(f"  API:       http://localhost:{config['api_port']}/state")
    print(f"  Debug:     {config['debug']}")
    print(f"  Preview:   {config['show_preview']}")
    print()

    # Graceful shutdown
    def signal_handler(sig, frame):
        print("\n[SHUTDOWN] Stopping...")
        if config["show_preview"]:
            cv2.destroyAllWindows()
        sys.exit(0)
    signal.signal(signal.SIGINT, signal_handler)

    # Start threads
    threads = [
        threading.Thread(target=stream_receiver_thread, args=(config,),
                         daemon=True, name="stream"),
        threading.Thread(target=face_tracking_thread, args=(config,),
                         daemon=True, name="tracking"),
        threading.Thread(target=rich_vision_thread, args=(config,),
                         daemon=True, name="rich"),
        threading.Thread(target=api_server_thread, args=(config,),
                         daemon=True, name="api"),
        threading.Thread(target=status_printer_thread, args=(config,),
                         daemon=True, name="status"),
    ]

    for t in threads:
        t.start()
        print(f"  Started thread: {t.name}")

    print("\n[READY] All threads running. Ctrl+C to stop.\n")

    # Keep main thread alive
    while True:
        time.sleep(1)


if __name__ == "__main__":
    main()
