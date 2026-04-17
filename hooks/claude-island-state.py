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

# Map hook events to status
STATUS_MAP = {
    "UserPromptSubmit": "busy",
    "PreToolUse": "busy",
    "PostToolUse": "busy",
    "PostToolUseFailure": "busy",
    "PermissionDenied": "busy",
    "PreCompact": "busy",
    "PostCompact": "busy",
    "SubagentStart": "busy",
    "SubagentStop": "busy",
    "Stop": "idle",
    "StopFailure": "idle",
    "SessionStart": "idle",
    "SessionEnd": "idle",
}

def write_status(status: str):
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

    # Handle Notification events specially
    if event == "Notification":
        if notification_type == "idle_prompt":
            write_status("idle")
        sys.exit(0)

    # Handle PermissionRequest - just let Claude Code handle it normally
    if event == "PermissionRequest":
        sys.exit(0)

    # Map event to status
    status = STATUS_MAP.get(event, "idle")
    write_status(status)

if __name__ == "__main__":
    main()
