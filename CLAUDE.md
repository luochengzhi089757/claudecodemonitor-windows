# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

A Windows taskbar status monitor for Claude Code, built on the [TrafficMonitor](https://github.com/zhongyang219/TrafficMonitor) plugin system. Two components communicate via a JSON file in `%TEMP%`:

1. **Python hook script** (`hooks/claude-island-state.py`) — receives Claude Code events on stdin, maps them to status, writes to `%TEMP%\claude-code-status.json`
2. **C++ DLL plugin** (`tm-plugin/ClaudeCodePlugin.cpp`) — polls the JSON file every 500ms, displays status in the taskbar via TrafficMonitor's plugin API

## Status States

| Status key | Display text | Trigger |
|---|---|---|
| `busy` | 工作中 | UserPromptSubmit, PreToolUse, PostToolUse (after approval) |
| `idle` | 待命 | SessionStart |
| `waiting` | 待命中 | Stop, Notification(idle_prompt) |
| `approval` | 等待批准 | PermissionRequest |
| `error` | 出错了 | StopFailure |
| `offline` | 离线中 | SessionEnd, or when claude.exe process not found |

## Build Commands

### Plugin DLL (C++)

Using MinGW (requires `g++` in PATH):
```bash
cd tm-plugin
build_mingw.bat
```

Using CMake:
```bash
cd tm-plugin
cmake -B build -G "MinGW Makefiles"
cmake --build build
```

Output: `tm-plugin/ClaudeCodePlugin.dll`

### Python hook

No build needed. Copy `hooks/claude-island-state.py` to `%USERPROFILE%\.claude\hooks\`.

## Architecture Details

- **Debouncing**: The DLL requires 2 consecutive identical readings (~1s) before updating the display, preventing flicker during rapid event sequences.
- **Process detection**: DLL falls back to checking if `claude.exe` is running via `CreateToolhelp32Snapshot` when the status file is missing or empty.
- **Hook event handling**: The Python script has special-case logic for `SessionStart` and `PreToolUse` to avoid overwriting `waiting` status when Stop fires shortly before these events (time-based guards at 2s and 30s respectively).
- **Plugin interface**: Uses TrafficMonitor plugin API v7 (`PluginInterface.h`). The DLL exports a single symbol `TMPluginGetInstance`.
