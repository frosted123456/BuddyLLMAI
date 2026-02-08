#!/usr/bin/env python3
"""
start_buddy.py — Launch all Buddy services
============================================

Starts buddy_vision.py and buddy_web_full_V2.py together.
Ctrl+C stops both.

Usage:
    python start_buddy.py --esp32-ip 192.168.1.100
    python start_buddy.py --esp32-ip 192.168.1.100 --rotate 90
    python start_buddy.py --esp32-ip 192.168.1.100 --no-vision
"""

import os
import subprocess
import sys
import signal
import time
import argparse

processes = []

def cleanup(sig=None, frame=None):
    print("\n[LAUNCHER] Stopping all services...")
    for p in processes:
        try:
            p.terminate()
            p.wait(timeout=5)
        except:
            p.kill()
    print("[LAUNCHER] All services stopped.")
    sys.exit(0)

signal.signal(signal.SIGINT, cleanup)
signal.signal(signal.SIGTERM, cleanup)

def main():
    parser = argparse.ArgumentParser(description="Launch all Buddy services")
    parser.add_argument("--esp32-ip", required=True, help="ESP32-S3 IP address")
    parser.add_argument("--rotate", type=int, default=0, help="Camera rotation (0/90/180/270)")
    parser.add_argument("--no-vision", action="store_true", help="Skip vision pipeline")
    args = parser.parse_args()

    print("=" * 46)
    print("  BUDDY LAUNCHER")
    print("=" * 46)
    print(f"  ESP32 IP:  {args.esp32_ip}")
    print(f"  Rotation:  {args.rotate}")
    print()

    # Start vision pipeline
    if not args.no_vision:
        print("[LAUNCHER] Starting vision pipeline...")
        vision_cmd = [
            sys.executable, "buddy_vision.py",
            "--esp32-ip", args.esp32_ip,
            "--rotate", str(args.rotate)
        ]
        p_vision = subprocess.Popen(vision_cmd)
        processes.append(p_vision)
        time.sleep(3)  # Let vision pipeline connect to stream

    # Start main server with ESP32 IP passed via environment
    print("[LAUNCHER] Starting main server...")
    server_env = os.environ.copy()
    server_env["BUDDY_ESP32_IP"] = args.esp32_ip
    server_cmd = [sys.executable, "buddy_web_full_V2.py"]
    p_server = subprocess.Popen(server_cmd, env=server_env)
    processes.append(p_server)

    print()
    print("[LAUNCHER] All services running. Ctrl+C to stop.")
    print()

    # Wait for any process to exit
    while True:
        for p in processes:
            if p.poll() is not None:
                print(f"[LAUNCHER] Process exited with code {p.returncode}")
                cleanup()
        time.sleep(1)

if __name__ == "__main__":
    main()
