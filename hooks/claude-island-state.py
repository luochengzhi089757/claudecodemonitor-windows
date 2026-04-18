#!/usr/bin/env python3
"""
Claude Code Hook for TrafficMonitor status indicator (Windows)
Receives hook events from Claude Code on stdin, maps them to status,
and writes to a status file that the TrafficMonitor plugin reads.
"""
import json
import os
import sys
import time
from pathlib import Path

STATUS_FILE = Path(os.environ.get("TEMP", "")) / "claude-code-status.json"
META_FILE = Path(os.environ.get("TEMP", "")) / "claude-hook-meta.json"
LOG_FILE = Path(os.environ.get("TEMP", "")) / "claude-hook-debug.log"

def read_meta():
    try:
        return json.loads(META_FILE.read_text(encoding="utf-8"))
    except Exception:
        return {}

def write_meta(meta: dict):
    try:
        META_FILE.write_text(json.dumps(meta), encoding="utf-8")
    except Exception:
        pass

def read_status():
    try:
        return json.loads(STATUS_FILE.read_text(encoding="utf-8"))
    except Exception:
        return {}

def write_status(status: str):
    meta = read_meta()
    meta["last_event"] = status
    meta["last_time"] = time.time()
    write_meta(meta)
    try:
        data = json.dumps({"status": status, "timestamp": int(time.time() * 1000)})
        STATUS_FILE.write_text(data, encoding="utf-8")
    except Exception:
        pass

def main():
    try:
        data = json.load(sys.stdin)
    except json.JSONDecodeError:
        sys.exit(1)

    event = data.get("hook_event_name", "")
    notification_type = data.get("notification_type", "")

    # Debug log
    try:
        with open(LOG_FILE, "a", encoding="utf-8") as f:
            f.write(f"[{time.strftime('%H:%M:%S')}] event={event} notification={notification_type}\n")
    except Exception:
        pass

    meta = read_meta()
    last_event = meta.get("last_event", "")
    last_time = meta.get("last_time", 0)
    now = time.time()

    # Handle Notification events specially
    if event == "Notification":
        if notification_type == "idle_prompt":
            write_status("waiting")
        sys.exit(0)

    # PermissionRequest → always write approval (user wants to see this)
    if event == "PermissionRequest":
        write_status("approval")
        sys.exit(0)

    # PreToolUse → write busy, but not if Stop fired within last 2 seconds
    if event == "PreToolUse":
        if last_event == "waiting" and (now - last_time) < 2:
            sys.exit(0)  # Skip: Stop just fired, don't overwrite "waiting"
        write_status("busy")
        sys.exit(0)

    # PostToolUse → restore busy after approval
    if event == "PostToolUse":
        if last_event == "approval":
            write_status("busy")  # User approved, Claude resumes working
        sys.exit(0)

    # SessionStart → write idle, but protect "waiting" from Stop
    if event == "SessionStart":
        if last_event == "waiting" and (now - last_time) < 30:
            sys.exit(0)  # Skip: Stop just fired, preserve "waiting"
        write_status("idle")
        sys.exit(0)

    # Map remaining key events
    status_map = {
        "UserPromptSubmit": "busy",
        "Stop": "waiting",
        "StopFailure": "error",
        "SessionEnd": "offline",
    }

    status = status_map.get(event)
    if status:
        write_status(status)

if __name__ == "__main__":
    main()
