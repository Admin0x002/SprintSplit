// SprintSplit - XInput1_4.dll proxy that separates sprint from dodge.
//
// How it works
// ------------
// ELDEN RING imports XInputGetState (ordinal 2) and XInputSetState (ordinal 3)
// from XInput1_4.dll. Because the executable imports those *by ordinal*, this
// DLL must export the same ordinal table; it then forwards every call to the
// real System32\XInput1_4.dll and only rewrites the button word returned by
// XInputGetState:
//
//   * The physical dodge button (B) is reported as one short tap, so the game
//     can only ever see "tap" -> backstep (standing) or roll (moving).
//   * A configurable sprint button is reported as a *held* B while the left
//     stick is deflected, which is exactly the input the game expects for
//     sprinting.
//
// No game code is patched, no anti-cheat bypass is involved: this only changes
// what the local gamepad reports to this process.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <xinput.h>

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>

#include "config.h"
#include "sprint_logic.h"

namespace {

typedef DWORD(WINAPI* FnGetState)(DWORD, XINPUT_STATE*);
typedef DWORD(WINAPI* FnSetState)(DWORD, XINPUT_VIBRATION*);
typedef DWORD(WINAPI* FnGetCaps)(DWORD, DWORD, XINPUT_CAPABILITIES*);
typedef void(WINAPI* FnEnable)(BOOL);
typedef DWORD(WINAPI* FnGetBattery)(DWORD, BYTE, XINPUT_BATTERY_INFORMATION*);
typedef DWORD(WINAPI* FnGetKeystroke)(DWORD, DWORD, XINPUT_KEYSTROKE*);
typedef DWORD(WINAPI* FnGetAudio)(DWORD, LPWSTR, UINT*, LPWSTR, UINT*);

HMODULE      g_real = nullptr;
FnGetState   g_get_state = nullptr;
FnGetState   g_get_state_ex = nullptr;
FnSetState   g_set_state = nullptr;
FnGetCaps    g_get_caps = nullptr;
FnEnable     g_enable = nullptr;
FnGetBattery g_get_battery = nullptr;
FnGetKeystroke g_get_keystroke = nullptr;
FnGetAudio   g_get_audio = nullptr;

ss::Config     g_config;
ss::Splitter   g_pads[4];
SRWLOCK        g_pads_lock = SRWLOCK_INIT;

HANDLE         g_log = INVALID_HANDLE_VALUE;
SRWLOCK        g_log_lock = SRWLOCK_INIT;
std::atomic<bool> g_ready(false);
std::atomic<bool> g_first_call(false);

INIT_ONCE      g_once = INIT_ONCE_STATIC_INIT;

void Log(const char* format, ...) {
    if (g_log == INVALID_HANDLE_VALUE) return;

    char line[1024];
    SYSTEMTIME st;
    GetLocalTime(&st);
    int used = snprintf(line, sizeof(line), "[%04d-%02d-%02d %02d:%02d:%02d] ",
                        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    if (used < 0) used = 0;
    if (static_cast<std::size_t>(used) >= sizeof(line)) return;

    va_list args;
    va_start(args, format);
    vsnprintf(line + used, sizeof(line) - static_cast<std::size_t>(used), format, args);
    va_end(args);

    std::size_t length = std::strlen(line);
    if (length + 1 >= sizeof(line)) length = sizeof(line) - 2;
    line[length++] = '\n';

    AcquireSRWLockExclusive(&g_log_lock);
    DWORD written = 0;
    WriteFile(g_log, line, static_cast<DWORD>(length), &written, nullptr);
    ReleaseSRWLockExclusive(&g_log_lock);
}

std::string Narrow(const std::wstring& text) {
    if (text.empty()) return std::string();
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0,
                                         nullptr, nullptr);
    if (size <= 1) return std::string();
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, &result[0], size, nullptr, nullptr);
    result.resize(static_cast<std::size_t>(size - 1));
    return result;
}

std::wstring ModuleDirectory() {
    wchar_t path[MAX_PATH] = {};
    HMODULE self = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&g_real), &self) ||
        self == nullptr) {
        return std::wstring();
    }
    if (GetModuleFileNameW(self, path, MAX_PATH) == 0) return std::wstring();

    std::wstring dir(path);
    const std::size_t slash = dir.find_last_of(L"\\/");
    if (slash != std::wstring::npos) dir.erase(slash);
    return dir;
}

void LoadRealXInput() {
    wchar_t system_dir[MAX_PATH] = {};
    const UINT length = GetSystemDirectoryW(system_dir, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        Log("GetSystemDirectoryW failed (%lu)", GetLastError());
        return;
    }

    std::wstring path(system_dir);
    path += L"\\XInput1_4.dll";
    g_real = LoadLibraryW(path.c_str());
    if (g_real == nullptr) {
        Log("cannot load the real XInput1_4.dll (%lu)", GetLastError());
        return;
    }

    g_get_state = reinterpret_cast<FnGetState>(GetProcAddress(g_real, "XInputGetState"));
    g_get_state_ex =
        reinterpret_cast<FnGetState>(GetProcAddress(g_real, MAKEINTRESOURCEA(100)));
    g_set_state = reinterpret_cast<FnSetState>(GetProcAddress(g_real, "XInputSetState"));
    g_get_caps = reinterpret_cast<FnGetCaps>(GetProcAddress(g_real, "XInputGetCapabilities"));
    g_enable = reinterpret_cast<FnEnable>(GetProcAddress(g_real, "XInputEnable"));
    g_get_battery =
        reinterpret_cast<FnGetBattery>(GetProcAddress(g_real, "XInputGetBatteryInformation"));
    g_get_keystroke =
        reinterpret_cast<FnGetKeystroke>(GetProcAddress(g_real, "XInputGetKeystroke"));
    g_get_audio =
        reinterpret_cast<FnGetAudio>(GetProcAddress(g_real, "XInputGetAudioDeviceIds"));

    if (g_get_state == nullptr) Log("the real XInputGetState is missing");
}

BOOL CALLBACK InitializeOnce(PINIT_ONCE, PVOID, PVOID*) {
    const std::wstring dir = ModuleDirectory();
    if (!dir.empty()) {
        const std::wstring config_dir = dir + L"\\SprintSplit";
        CreateDirectoryW(config_dir.c_str(), nullptr);
    }
    g_config = ss::LoadConfig(dir);

    if (g_config.log_enabled) {
        g_log = CreateFileW(g_config.log_path.c_str(), FILE_APPEND_DATA,
                            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
    }

    Log("SprintSplit starting (%s), module dir %s",
        g_config.loaded ? "SprintSplit.ini loaded" : "defaults, no ini",
        Narrow(dir).c_str());
    if (!g_config.status.empty()) Log("config note: %s", g_config.status.c_str());
    Log("sprint=0x%04X deadzone=%.2f tap=%dms preGap=%dms postGap=%dms release=%dms "
        "resume=%d suppress=%d enabled=%d",
        static_cast<unsigned>(g_config.options.sprint_mask),
        static_cast<double>(g_config.options.deadzone),
        g_config.options.tap_ms, g_config.options.pre_gap_ms,
        g_config.options.post_gap_ms, g_config.options.sprint_release_ms,
        g_config.options.resume_sprint ? 1 : 0,
        g_config.options.suppress_sprint_btn ? 1 : 0,
        g_config.options.enabled ? 1 : 0);

    LoadRealXInput();

    const ULONGLONG now = GetTickCount64();
    for (int i = 0; i < 4; ++i) g_pads[i].Reset(now);

    g_ready.store(true);
    return TRUE;
}

inline void EnsureInitialized() {
    if (g_ready.load(std::memory_order_acquire)) return;
    InitOnceExecuteOnce(&g_once, InitializeOnce, nullptr, nullptr);
}

DWORD WINAPI GetStateCommon(DWORD index, XINPUT_STATE* state, bool extended) {
    EnsureInitialized();

    FnGetState function = extended ? g_get_state_ex : g_get_state;
    if (function == nullptr) return ERROR_DEVICE_NOT_CONNECTED;

    const DWORD result = function(index, state);
    if (result != ERROR_SUCCESS || state == nullptr) return result;

    if (!g_first_call.exchange(true)) {
        Log("first XInputGetState call, pad %lu, buttons=0x%04X", index,
            static_cast<unsigned>(state->Gamepad.wButtons));
    }

    const ss::Options& options = g_config.options;
    if (!options.enabled || index > 3) return result;
    if (options.user_index >= 0 && static_cast<DWORD>(options.user_index) != index) {
        return result;
    }

    ss::Input input;
    input.buttons = state->Gamepad.wButtons;
    input.lx = state->Gamepad.sThumbLX;
    input.ly = state->Gamepad.sThumbLY;

    ss::Output output;
    AcquireSRWLockExclusive(&g_pads_lock);
    output = g_pads[index].Update(input, options, GetTickCount64());
    ReleaseSRWLockExclusive(&g_pads_lock);

    if (output.modified) {
        state->Gamepad.wButtons = output.buttons;
        state->dwPacketNumber = static_cast<WORD>(state->dwPacketNumber + 1);
    }
    return result;
}

}  // namespace

// The exported entry points are implemented under private ss_* names and the
// public names are attached in src/proxy.def.  The system names cannot be
// defined directly here: xinput.h already declares them, and recent Windows
// SDKs (10.0.26100 and later) declare them with an exception specification
// that a plain definition cannot match, i.e. MSVC rejects it with
// "C2382: redefinition; different exception specifications".
extern "C" DWORD WINAPI ss_XInputGetState(DWORD dwUserIndex, XINPUT_STATE* pState) {
    return GetStateCommon(dwUserIndex, pState, false);
}

extern "C" DWORD WINAPI ss_XInputGetStateEx(DWORD dwUserIndex, XINPUT_STATE* pState) {
    return GetStateCommon(dwUserIndex, pState, true);
}

extern "C" DWORD WINAPI ss_XInputSetState(DWORD dwUserIndex, XINPUT_VIBRATION* pVibration) {
    EnsureInitialized();
    if (g_set_state == nullptr) return ERROR_DEVICE_NOT_CONNECTED;
    return g_set_state(dwUserIndex, pVibration);
}

extern "C" DWORD WINAPI ss_XInputGetCapabilities(DWORD dwUserIndex, DWORD dwFlags,
                                              XINPUT_CAPABILITIES* pCapabilities) {
    EnsureInitialized();
    if (g_get_caps == nullptr) return ERROR_DEVICE_NOT_CONNECTED;
    return g_get_caps(dwUserIndex, dwFlags, pCapabilities);
}

extern "C" void WINAPI ss_XInputEnable(BOOL enable) {
    EnsureInitialized();
    if (g_enable != nullptr) g_enable(enable);
}

extern "C" DWORD WINAPI ss_XInputGetBatteryInformation(
    DWORD dwUserIndex, BYTE devType, XINPUT_BATTERY_INFORMATION* pBatteryInformation) {
    EnsureInitialized();
    if (g_get_battery == nullptr) return ERROR_DEVICE_NOT_CONNECTED;
    return g_get_battery(dwUserIndex, devType, pBatteryInformation);
}

extern "C" DWORD WINAPI ss_XInputGetKeystroke(DWORD dwUserIndex, DWORD dwReserved,
                                           XINPUT_KEYSTROKE* pKeystroke) {
    EnsureInitialized();
    if (g_get_keystroke == nullptr) return ERROR_DEVICE_NOT_CONNECTED;
    return g_get_keystroke(dwUserIndex, dwReserved, pKeystroke);
}

extern "C" DWORD WINAPI ss_XInputGetAudioDeviceIds(DWORD dwUserIndex, LPWSTR pRenderDeviceId,
                                                UINT* pRenderCount, LPWSTR pCaptureDeviceId,
                                                UINT* pCaptureCount) {
    EnsureInitialized();
    if (g_get_audio == nullptr) return ERROR_DEVICE_NOT_CONNECTED;
    return g_get_audio(dwUserIndex, pRenderDeviceId, pRenderCount, pCaptureDeviceId,
                       pCaptureCount);
}

// Undocumented exports of XInput1_4.dll. ELDEN RING does not import them, but
// they exist in the system DLL; keep the ordinal table complete so that other
// modules which import them still load.
extern "C" DWORD WINAPI ss_XInputWaitForGuideButton(DWORD, DWORD, void*) {
    return ERROR_DEVICE_NOT_CONNECTED;
}

extern "C" DWORD WINAPI ss_XInputCancelGuideButtonWait(DWORD) {
    return ERROR_DEVICE_NOT_CONNECTED;
}

extern "C" DWORD WINAPI ss_XInputPowerOffController(DWORD) {
    return ERROR_DEVICE_NOT_CONNECTED;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}
