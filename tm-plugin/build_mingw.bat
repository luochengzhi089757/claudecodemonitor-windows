@echo off
REM Build script using MinGW (g++)
REM Requires: MinGW-w64 with g++ in PATH

echo Building ClaudeCodePlugin.dll with MinGW...

g++ -shared -static-libgcc -static-libstdc++ -O2 ^
    -D TMPLUGIN_EXPORTS ^
    -o ClaudeCodePlugin.dll ^
    ClaudeCodePlugin.cpp ^
    ClaudeCodePlugin.def

if %errorlevel% neq 0 (
    echo Build failed. Make sure g++ (MinGW) is installed and in PATH.
    pause
    exit /b 1
)

echo.
echo BUILD SUCCESS: ClaudeCodePlugin.dll created
echo.
echo INSTALL:
echo   Copy ClaudeCodePlugin.dll to:
echo   ^<TrafficMonitor install dir^>\plugins\
echo Then restart TrafficMonitor and enable the plugin in Settings.
pause
