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
is mounted sideways, matching the old ESP32 firmware's 90deg CCW rotation).
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
from flask import Flask, jsonify, Response

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
    # Set to 90 if camera is mounted sideways (old ESP32 did 90deg CCW)
    "rotation": 0,

    # Detection settings
    "detection_confidence": 0.5,
    "tracking_confidence": 0.5,
    "max_faces": 3,

    # Performance
    "target_tracking_fps": 30,    # Face detection rate
    "target_rich_fps": 3,         # Object/expression rate
    "proxy_stream_fps": 12,       # Annotated MJPEG proxy stream FPS

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

        # Annotated frame (with bounding boxes drawn, for proxy stream)
        self.annotated_frame = None

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

        # Head pose (from solvePnP on face mesh landmarks)
        self.head_yaw = 0.0           # degrees, 0 = facing camera
        self.head_pitch = 0.0         # degrees, 0 = level
        self.facing_camera = False    # True if yaw within ±20° AND pitch within ±15°
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
        self.reconnect_count = 0

        # Sequence counter (monotonic, matches Teensy expectation)
        self.sequence = 0

        # Last UDP message sent to ESP32
        self.last_udp_msg = ""

        # Coordinate history — rolling buffer for /coord_history endpoint
        self.coord_history = deque(maxlen=300)

        # ── Response Detection (for narrative engine) ──
        self.expression_history = deque(maxlen=30)   # (expression, timestamp)
        self.last_stable_expression = "neutral"
        self.expression_changed_at = 0
        self.gaze_direction = "center"               # left / center / right
        self.person_approached = False
        self.person_left_at = 0
        self.face_appeared_at = 0
        self._prev_face_detected = False

    def update_frame(self, frame):
        with self.lock:
            self.latest_frame = frame
            self.frame_timestamp = time.time()
            self.frame_count += 1

    def get_frame(self):
        with self.lock:
            return self.latest_frame, self.frame_timestamp

    def update_annotated_frame(self, frame):
        with self.lock:
            self.annotated_frame = frame

    def get_annotated_frame(self):
        with self.lock:
            if self.annotated_frame is not None:
                return self.annotated_frame.copy()
            return None

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

    def record_coord(self, face_x, face_y, vx, vy, conf, face_detected, udp_msg):
        """Append a coordinate data point to the rolling history buffer."""
        with self.lock:
            self.coord_history.append({
                "timestamp": time.time(),
                "face_x": face_x,
                "face_y": face_y,
                "vx": vx,
                "vy": vy,
                "conf": conf,
                "face_detected": face_detected,
                "last_udp_msg": udp_msg,
            })

    def get_coord_history(self):
        with self.lock:
            return list(self.coord_history)

    def get_state_dict(self):
        """Full state for API endpoint."""
        with self.lock:
            # Flatten primary_face fields to top level for easy dashboard access
            pf = self.primary_face or {}
            return {
                "face_detected": self.face_detected,
                "primary_face": self.primary_face,
                "face_x": pf.get("x", 0),
                "face_y": pf.get("y", 0),
                "face_vx": pf.get("vx", 0),
                "face_vy": pf.get("vy", 0),
                "face_w": pf.get("w", 0),
                "face_h": pf.get("h", 0),
                "confidence": pf.get("conf", 0),
                "sequence": self.sequence,
                "face_count": len(self.faces),
                "tracking_fps": round(self.tracking_fps, 1),
                "detection_fps": round(self.tracking_fps, 1),  # alias for dashboard
                "detection_latency_ms": round(self.detection_latency_ms, 1),
                "stream_connected": self.stream_connected,
                "stream_fps": round(self.stream_fps, 1),
                "frame_count": self.frame_count,
                "udp_sent": self.udp_sent,
                "reconnect_count": self.reconnect_count,
                "objects": self.objects,
                "face_expression": self.face_expression,
                "head_yaw": round(self.head_yaw, 1),
                "head_pitch": round(self.head_pitch, 1),
                "facing_camera": self.facing_camera,
                "scene_novelty": round(self.scene_novelty, 2),
                "person_count": self.person_count,
                "errors": self.errors,
                "last_error": self.last_error,
                "last_udp_msg": self.last_udp_msg,
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
        < 25:  REJECTED
        25-69: Low confidence (was histogram tracker range)
        70-95: High confidence (was AI detection range)

    MediaPipe typically returns 0.5-0.99.
    We map to a wider range to give Teensy meaningful variation:
        0.50 -> 55   (low — barely detected)
        0.65 -> 65   (low-medium)
        0.75 -> 75   (medium-high)
        0.85 -> 82   (high)
        0.95 -> 90   (very high)
        1.00 -> 95   (maximum)
    """
    # Piecewise linear mapping for better spread
    if detection_score < 0.5:
        base_conf = 25  # Minimum accepted
    elif detection_score < 0.7:
        # 0.5-0.7 -> 55-70 (low confidence band)
        base_conf = int(55 + (detection_score - 0.5) * 75)
    elif detection_score < 0.85:
        # 0.7-0.85 -> 70-85 (high confidence band)
        base_conf = int(70 + (detection_score - 0.7) * 100)
    else:
        # 0.85-1.0 -> 85-95 (very high confidence band)
        base_conf = int(85 + (detection_score - 0.85) * 67)

    base_conf = max(25, min(base_conf, 95))

    # Small boost for larger faces (closer = more reliable)
    if face_w_240 > 80:
        base_conf = min(base_conf + 3, 98)
    elif face_w_240 > 60:
        base_conf = min(base_conf + 2, 98)

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

            # Use FFMPEG backend for better MJPEG handling and timeout support
            cap = cv2.VideoCapture(url, cv2.CAP_FFMPEG)
            cap.set(cv2.CAP_PROP_BUFFERSIZE, 1)  # Phase 1D: BUG-5 fix — minimize internal buffering
            cap.set(cv2.CAP_PROP_OPEN_TIMEOUT_MSEC, 10000)   # 10s connection timeout
            cap.set(cv2.CAP_PROP_READ_TIMEOUT_MSEC, 5000)    # 5s read timeout

            if not cap.isOpened():
                print("[STREAM] Failed to open stream, retrying in 3s...")
                state.stream_connected = False
                state.reconnect_count += 1
                time.sleep(3)
                continue

            print("[STREAM] Connected!")
            state.stream_connected = True

            while cap.isOpened():
                ret, frame = cap.read()
                if not ret:
                    print("[STREAM] Frame read failed, reconnecting...")
                    state.reconnect_count += 1
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
            state.reconnect_count += 1

        time.sleep(2)  # Wait before reconnect


# ════════════════════════════════════════════════════════════════
# ANNOTATION HELPER
# ════════════════════════════════════════════════════════════════

def draw_annotations(frame, detections, frame_w, frame_h, detect_ms, face_landmarks_available):
    """
    Draw face detection bounding boxes, landmarks dots, and overlay text
    onto a frame. Returns the annotated copy.

    Args:
        frame: BGR image (will be copied, not modified in place)
        detections: list of mediapipe detection objects (or None)
        frame_w, frame_h: frame dimensions
        detect_ms: detection latency in milliseconds
        face_landmarks_available: whether face landmarks flag is set
    """
    annotated = frame.copy()

    face_count = 0

    if detections:
        face_count = len(detections)
        for det in detections:
            bbox = det.location_data.relative_bounding_box
            score = det.score[0]

            x1 = int(bbox.xmin * frame_w)
            y1 = int(bbox.ymin * frame_h)
            x2 = int((bbox.xmin + bbox.width) * frame_w)
            y2 = int((bbox.ymin + bbox.height) * frame_h)

            # Green bounding box
            cv2.rectangle(annotated, (x1, y1), (x2, y2), (0, 255, 0), 2)

            # Confidence label above box
            label = f"{score:.2f}"
            cv2.putText(annotated, label,
                        (x1, y1 - 10), cv2.FONT_HERSHEY_SIMPLEX,
                        0.6, (0, 255, 0), 2)

            # Draw landmark dots (keypoints) if available in detection
            if det.location_data.relative_keypoints:
                for kp in det.location_data.relative_keypoints:
                    kx = int(kp.x * frame_w)
                    ky = int(kp.y * frame_h)
                    cv2.circle(annotated, (kx, ky), 3, (255, 0, 255), -1)

    # Overlay text info bar at top
    fps_val = state.tracking_fps
    overlay_text = (
        f"FPS: {fps_val:.1f} | "
        f"Det: {detect_ms:.0f}ms | "
        f"Faces: {face_count} | "
        f"UDP: {state.udp_sent}"
    )
    cv2.putText(annotated, overlay_text,
                (10, 30), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 255), 2)

    return annotated


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

                    # Store last UDP message
                    state.last_udp_msg = msg

                    # Record coordinate history
                    state.record_coord(tx, ty, vx, vy, conf, True, msg)

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

                # Store last UDP message
                state.last_udp_msg = msg

                # Record coordinate history (no face)
                state.record_coord(120, 120, 0, 0, 0, False, msg)

                state.update_tracking([], None, 0, detect_ms)

                # Reset velocity tracking after sustained face loss
                if consecutive_no_face > 10:
                    with state.lock:
                        state.prev_face_time = 0
                        state.prev_face_x = 120
                        state.prev_face_y = 120

                if config["debug"] and consecutive_no_face == 1:
                    print(f"[TRACKING] Face lost ({detect_ms:.1f}ms)")

            # -- Build and store annotated frame for proxy stream --
            annotated = draw_annotations(
                frame, results.detections, frame_w, frame_h,
                detect_ms, state.face_landmarks is not None
            )
            state.update_annotated_frame(annotated)

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

    # Phase 2: UDP socket for sending VISION updates to ESP32 -> Teensy
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
                # Compute expression and head pose outside lock (pure computation)
                landmarks = mesh_results.multi_face_landmarks[0]
                expression = estimate_expression(landmarks, frame_w, frame_h)
                yaw, pitch = estimate_head_pose(landmarks, frame_w, frame_h)
                person_count = len(mesh_results.multi_face_landmarks)

                # Write all state fields under lock for consistent API snapshots
                with state.lock:
                    state.person_count = person_count
                    state.face_expression = expression
                    state.face_landmarks = True
                    state.head_yaw = yaw
                    state.head_pitch = pitch
                    state.facing_camera = abs(yaw) < 20.0 and abs(pitch) < 15.0
            else:
                with state.lock:
                    state.person_count = 0
                    state.face_expression = None
                    state.face_landmarks = None
                    state.head_yaw = 0.0
                    state.head_pitch = 0.0
                    state.facing_camera = False

            # -- Response Detection: track expression changes --
            if expression:
                now_ts = time.time()
                with state.lock:
                    state.expression_history.append((expression, now_ts))

                    # Check for stable expression change
                    if expression != state.last_stable_expression:
                        # Check if the new expression has been consistent for >1s
                        recent_exprs = [
                            e for e, t in state.expression_history
                            if now_ts - t < 2.0
                        ]
                        if recent_exprs and all(e == expression for e in recent_exprs[-3:]):
                            state.last_stable_expression = expression
                            state.expression_changed_at = now_ts

                    # Track face appeared/left transitions
                    if state.face_detected and not state._prev_face_detected:
                        state.face_appeared_at = now_ts
                    elif not state.face_detected and state._prev_face_detected:
                        state.person_left_at = now_ts
                    state._prev_face_detected = state.face_detected

            # -- Scene Novelty Detection --
            gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
            gray_small = cv2.resize(gray, (160, 120))

            if prev_frame_gray is not None:
                diff = cv2.absdiff(prev_frame_gray, gray_small)
                novelty = float(np.mean(diff)) / 255.0
                # Smooth the novelty signal
                state.scene_novelty = state.scene_novelty * 0.7 + novelty * 0.3

            prev_frame_gray = gray_small

            # -- Phase 2: Send VISION update to Teensy via UDP --
            # This closes the observation loop: PC sees -> Teensy feels
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


def estimate_head_pose(landmarks, frame_w, frame_h):
    """
    Estimate head pose (yaw, pitch) from MediaPipe face mesh landmarks
    using cv2.solvePnP with 6 key facial points.

    Returns: (yaw_degrees, pitch_degrees) or (0.0, 0.0) on failure.
    Yaw: 0 = facing camera, positive = turned right, negative = turned left.
    Pitch: 0 = level, positive = looking down, negative = looking up.
    """
    try:
        # 3D model points (generic face model in arbitrary units)
        # X-axis matches image space: positive X = right side of image.
        # MediaPipe convention: subject's left eye appears on image RIGHT side.
        model_points = np.array([
            (0.0, 0.0, 0.0),        # Nose tip (landmark 1)
            (0.0, -63.6, -12.5),     # Chin (landmark 152)
            (43.3, 32.7, -26.0),     # Left eye outer (landmark 263) — appears image-right
            (-43.3, 32.7, -26.0),    # Right eye outer (landmark 33) — appears image-left
            (28.9, -28.9, -24.1),    # Left mouth corner (landmark 291) — appears image-right
            (-28.9, -28.9, -24.1),   # Right mouth corner (landmark 61) — appears image-left
        ], dtype=np.float64)

        # Corresponding 2D image points from face mesh landmarks
        landmark_indices = [1, 152, 263, 33, 291, 61]
        image_points = np.array([
            (landmarks.landmark[i].x * frame_w,
             landmarks.landmark[i].y * frame_h)
            for i in landmark_indices
        ], dtype=np.float64)

        # Camera matrix (approximate, assuming no lens distortion)
        focal_length = frame_w
        center = (frame_w / 2, frame_h / 2)
        camera_matrix = np.array([
            [focal_length, 0, center[0]],
            [0, focal_length, center[1]],
            [0, 0, 1],
        ], dtype=np.float64)

        dist_coeffs = np.zeros((4, 1), dtype=np.float64)

        success, rotation_vec, _ = cv2.solvePnP(
            model_points, image_points, camera_matrix, dist_coeffs,
            flags=cv2.SOLVEPNP_ITERATIVE
        )

        if not success:
            return 0.0, 0.0

        # Convert rotation vector to rotation matrix
        rotation_mat, _ = cv2.Rodrigues(rotation_vec)

        # Decompose rotation matrix to Euler angles
        # Use projectPoints approach for yaw/pitch extraction
        sy = np.sqrt(rotation_mat[0, 0] ** 2 + rotation_mat[1, 0] ** 2)
        if sy > 1e-6:
            pitch = np.degrees(np.arctan2(rotation_mat[2, 1], rotation_mat[2, 2]))
            yaw = np.degrees(np.arctan2(-rotation_mat[2, 0], sy))
        else:
            pitch = np.degrees(np.arctan2(-rotation_mat[1, 2], rotation_mat[1, 1]))
            yaw = np.degrees(np.arctan2(-rotation_mat[2, 0], sy))

        # Guard against NaN from degenerate geometry
        if not (np.isfinite(yaw) and np.isfinite(pitch)):
            return 0.0, 0.0

        return float(yaw), float(pitch)

    except Exception:
        return 0.0, 0.0


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
            "head_yaw": round(state.head_yaw, 1),
            "head_pitch": round(state.head_pitch, 1),
            "facing_camera": state.facing_camera,
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
        "reconnect_count": state.reconnect_count,
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


@api_app.route('/annotated_snapshot')
def get_annotated_snapshot():
    """Latest annotated frame (with face bounding boxes) as JPEG."""
    annotated = state.get_annotated_frame()
    if annotated is None:
        # Fall back to raw frame if no annotated frame yet
        frame, _ = state.get_frame()
        if frame is None:
            return "No frame available", 503
        annotated = frame

    _, buf = cv2.imencode('.jpg', annotated, [cv2.IMWRITE_JPEG_QUALITY, 85])
    return buf.tobytes(), 200, {'Content-Type': 'image/jpeg'}


def _generate_mjpeg_stream(target_fps):
    """Generator that yields annotated frames as MJPEG multipart chunks."""
    frame_interval = 1.0 / target_fps
    while True:
        start = time.time()

        annotated = state.get_annotated_frame()
        if annotated is None:
            # Fall back to raw frame if annotated not yet available
            frame, _ = state.get_frame()
            if frame is None:
                time.sleep(0.05)
                continue
            annotated = frame

        _, buf = cv2.imencode('.jpg', annotated, [cv2.IMWRITE_JPEG_QUALITY, 75])
        jpg_bytes = buf.tobytes()

        yield (
            b'--frame\r\n'
            b'Content-Type: image/jpeg\r\n'
            b'Content-Length: ' + str(len(jpg_bytes)).encode() + b'\r\n'
            b'\r\n' + jpg_bytes + b'\r\n'
        )

        # Throttle to target FPS
        elapsed = time.time() - start
        sleep_time = frame_interval - elapsed
        if sleep_time > 0:
            time.sleep(sleep_time)


@api_app.route('/stream')
def get_stream():
    """
    Annotated MJPEG proxy stream.
    Re-serves the ESP32 camera stream with face detection overlays drawn.
    Supports multiple simultaneous clients (solves single-client limitation).
    """
    target_fps = DEFAULT_CONFIG.get("proxy_stream_fps", 12)
    return Response(
        _generate_mjpeg_stream(target_fps),
        mimetype='multipart/x-mixed-replace; boundary=frame',
        headers={
            'Access-Control-Allow-Origin': '*',
            'Cache-Control': 'no-cache, no-store, must-revalidate',
            'Pragma': 'no-cache',
            'Expires': '0',
        }
    )


@api_app.route('/coord_history')
def get_coord_history():
    """Rolling buffer of recent coordinate data points as JSON array."""
    return jsonify(state.get_coord_history())


@api_app.route('/response_detection')
def get_response_detection():
    """
    Response detection state for the narrative engine.
    Reports whether the person looked at Buddy, smiled, changed expression, etc.
    """
    with state.lock:
        return jsonify({
            "face_detected": state.face_detected,
            "expression": state.face_expression,
            "last_stable_expression": state.last_stable_expression,
            "expression_changed_at": state.expression_changed_at,
            "face_appeared_at": state.face_appeared_at,
            "person_left_at": state.person_left_at,
            "person_count": state.person_count,
            "scene_novelty": round(state.scene_novelty, 2),
            "head_yaw": round(state.head_yaw, 1),
            "head_pitch": round(state.head_pitch, 1),
            "facing_camera": state.facing_camera,
        })


@api_app.route('/last_udp_msg')
def get_last_udp_msg():
    """Returns the last FACE: or NO_FACE string that was sent to ESP32."""
    with state.lock:
        return jsonify({
            "last_udp_msg": state.last_udp_msg,
            "udp_sent": state.udp_sent,
        })


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
              f"Reconnects:{s['reconnect_count']} | "
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
    print("|  BUDDY VISION PIPELINE v1.1                |")
    print("+" + "=" * 44 + "+")
    print(f"  ESP32:     {config['esp32_ip']}")
    print(f"  Stream:    http://{config['esp32_ip']}/stream")
    print(f"  UDP out:   {config['esp32_ip']}:{config['udp_port']}")
    print(f"  Rotation:  {config['rotation']}deg")
    print(f"  API:       http://localhost:{config['api_port']}/state")
    print(f"  Proxy:     http://localhost:{config['api_port']}/stream")
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
