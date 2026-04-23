// ClaudeCodeStatus — TrafficMonitor Plugin
// Reads status from %TEMP%\claude-code-status.json written by Claude Code hooks.
// Status mapping:
//   busy/working → 工作中
//   idle         → 待命中
//   waiting      → 待命中 (Claude completed, waiting for user)
//   approval     → 等待批准 (Claude requesting permission)
//   offline      → 离线中

#include "PluginInterface.h"
#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <string>
#include <mutex>
#include <atomic>
#include <thread>
#include <stdio.h>
#include <time.h>

static std::mutex   g_statusMutex;
static std::wstring g_currentText = L"离线中";
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

    auto pos = content.find("\"status\"");
    if (pos == std::string::npos) return L"";
    auto colon = content.find(':', pos);
    if (colon == std::string::npos) return L"";
    auto q1 = content.find('"', colon);
    if (q1 == std::string::npos) return L"";
    auto q2 = content.find('"', q1 + 1);
    if (q2 == std::string::npos) return L"";

    std::string status = content.substr(q1 + 1, q2 - q1 - 1);
    if (status == "busy" || status == "working") return L"工作中";
    if (status == "idle") return L"待命中";
    if (status == "waiting") return L"待命中";
    if (status == "approval") return L"等待批准";
    if (status == "error") return L"出错了";
    if (status == "offline") return L"离线中";
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

// ── Windows balloon notification (via Shell_NotifyIconW) ────────────

static time_t g_lastNotifyTime = 0;

struct BalloonData {
    wchar_t title[64];
    wchar_t msg[256];
};

static DWORD WINAPI BalloonThread(LPVOID param) {
    BalloonData* d = (BalloonData*)param;

    // Create a temporary window for the tray icon
    HWND hWnd = CreateWindowExW(0, L"STATIC", L"", WS_POPUP,
        0, 0, 0, 0, NULL, NULL, GetModuleHandle(NULL), NULL);

    HICON hIcon = LoadIcon(NULL, IDI_INFORMATION);

    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hWnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_INFO | NIF_TIP;
    nid.hIcon = hIcon;
    nid.dwInfoFlags = NIIF_INFO;
    nid.uTimeout = 10000;
    lstrcpynW(nid.szInfoTitle, d->title, ARRAYSIZE(nid.szInfoTitle));
    lstrcpynW(nid.szInfo, d->msg, ARRAYSIZE(nid.szInfo));
    lstrcpynW(nid.szTip, L"Claude Code", ARRAYSIZE(nid.szTip));

    Shell_NotifyIconW(NIM_ADD, &nid);

    // Wait for balloon to timeout or user interaction
    Sleep(10000);

    // Clean up
    Shell_NotifyIconW(NIM_DELETE, &nid);
    DestroyIcon(hIcon);
    DestroyWindow(hWnd);

    delete d;
    return 0;
}

static void ShowNotification(const wchar_t* title, const wchar_t* msg) {
    time_t now = time(NULL);
    if (now - g_lastNotifyTime < 10) return; // throttle: min 10s
    g_lastNotifyTime = now;

    BalloonData* d = new BalloonData;
    lstrcpynW(d->title, title, ARRAYSIZE(d->title));
    lstrcpynW(d->msg, msg, ARRAYSIZE(d->msg));

    CreateThread(NULL, 0, BalloonThread, d, 0, NULL);
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
            new_text = L"离线中";
        } else {
            // Priority 2: read status file
            new_text = ReadStatusFile();
            if (new_text.empty()) {
                new_text = L"待命中";
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
            // Trigger notification when transitioning to "待命中" from a different state
            bool shouldNotify = (new_text == L"待命中" && last_status != L"待命中");

            {
                std::lock_guard<std::mutex> lock(g_statusMutex);
                g_currentText = new_text;
                last_status = new_text;
            }

            if (shouldNotify) {
                ShowNotification(L"Claude Code", L"Claude 已回复完毕");
            }
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
    const wchar_t* GetItemValueSampleText() const override { return L"待命中"; }
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
            case TMI_VERSION:     return L"1.0.3";
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
