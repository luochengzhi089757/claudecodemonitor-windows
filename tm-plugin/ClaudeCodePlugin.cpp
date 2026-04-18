// ClaudeCodeStatus — TrafficMonitor Plugin v4.0
// Reads status from %TEMP%\claude-code-status.json written by Claude Code hooks.
// Status mapping:
//   busy/working → 工作中
//   idle         → 待命
//   waiting      → 等待回复 (Claude completed, waiting for user)
//   approval     → 等待批准 (Claude requesting permission)
//   offline      → 离线

#include "PluginInterface.h"
#include <windows.h>
#include <tlhelp32.h>
#include <string>
#include <mutex>
#include <atomic>
#include <thread>
#include <vector>
#include <stdio.h>

static std::mutex   g_statusMutex;
static std::wstring g_currentText = L"\u79bb\u7ebf";
static std::atomic<bool> g_pollRunning{ true };

static std::wstring GetStatusFilePath() {
    wchar_t tempPath[MAX_PATH];
    GetTempPathW(MAX_PATH, tempPath);
    return std::wstring(tempPath) + L"\\claude-code-status.json";
}

// Simple JSON parsing without dependencies
static std::wstring ReadStatusFile() {
    auto path = GetStatusFilePath();
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) return L"";

    char buf[256];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';
    std::string content(buf, n);

    // Find "status" value
    auto pos = content.find("\"status\"");
    if (pos == std::string::npos) return L"";
    auto colon = content.find(':', pos);
    if (colon == std::string::npos) return L"";
    auto q1 = content.find('"', colon);
    if (q1 == std::string::npos) return L"";
    auto q2 = content.find('"', q1 + 1);
    if (q2 == std::string::npos) return L"";

    std::string status = content.substr(q1 + 1, q2 - q1 - 1);
    if (status == "busy" || status == "working") return L"\u5de5\u4f5c\u4e2d";
    if (status == "idle") return L"\u5f85\u547d";
    if (status == "waiting") return L"\u7b49\u5f85\u56de\u590d";
    if (status == "approval") return L"\u7b49\u5f85\u6279\u51c6";
    if (status == "error") return L"\u51fa\u9519\u4e86";
    if (status == "offline") return L"\u79bb\u7ebf";
    return L"";
}

// Fallback: check if claude.exe is running
static bool HasClaudeProcess() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W pe = { 0 };
    pe.dwSize = sizeof(pe);
    bool found = false;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, L"claude.exe") == 0 ||
                _wcsicmp(pe.szExeFile, L"claude.cmd") == 0) {
                found = true;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return found;
}

static void PollThread() {
    std::wstring last_status;
    std::wstring pending_status;
    int stable_count = 0;

    while (g_pollRunning) {
        Sleep(500);

        std::wstring new_text;

        // Priority 1: if Claude process doesn't exist, always show offline
        if (!HasClaudeProcess()) {
            new_text = L"\u79bb\u7ebf";
        } else {
            // Priority 2: read status file for working/idle
            new_text = ReadStatusFile();
            if (new_text.empty()) {
            new_text = L"\u5f85\u547d";
            }
        }

        // Debounce: require 2 consecutive same values before updating
        if (new_text == pending_status) {
            stable_count++;
        } else {
            pending_status = new_text;
            stable_count = 1;
        }

        if (stable_count >= 2 && new_text != last_status) {
            std::lock_guard<std::mutex> lock(g_statusMutex);
            g_currentText = new_text;
            last_status = new_text;
        }
    }
}

// ── Plugin Item ───────────────────────────────────────────────

class CPluginItem : public IPluginItem {
public:
    const wchar_t* GetItemName() const override { return L"Claude Code"; }
    const wchar_t* GetItemId() const override { return L"claude_code_status"; }
    const wchar_t* GetItemLableText() const override { return L"Claude:"; }
    const wchar_t* GetItemValueText() const override {
        std::lock_guard<std::mutex> lock(g_statusMutex);
        return g_currentText.c_str();
    }
    const wchar_t* GetItemValueSampleText() const override { return L"\u7b49\u5f85\u56de\u590d"; }
    bool IsCustomDraw() const override { return false; }
};

class CClaudeCodePlugin : public ITMPlugin {
public:
    CClaudeCodePlugin() {
        std::thread(PollThread).detach();
    }
    ~CClaudeCodePlugin() {
        g_pollRunning = false;
    }
    IPluginItem* GetItem(int index) override {
        if (index == 0) return &m_item;
        return nullptr;
    }
    void DataRequired() override {}
    const wchar_t* GetInfo(PluginInfoIndex index) override {
        switch (index) {
            case TMI_NAME:        return L"Claude Code Status";
            case TMI_DESCRIPTION: return L"Shows Claude Code activity status";
            case TMI_AUTHOR:      return L"You";
            case TMI_COPYRIGHT:   return L"";
            case TMI_VERSION:     return L"1.0.1";
            case TMI_URL:         return L"";
            default:              return L"";
        }
    }
    const wchar_t* GetTooltipInfo() override {
        std::lock_guard<std::mutex> lock(g_statusMutex);
        static wchar_t buf[256];
        swprintf(buf, 256, L"Claude Code: %s", g_currentText.c_str());
        return buf;
    }
private:
    CPluginItem m_item;
};

static CClaudeCodePlugin g_plugin;

extern "C" __declspec(dllexport) ITMPlugin* TMPluginGetInstance() {
    return &g_plugin;
}
