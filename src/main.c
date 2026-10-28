#include <windows.h>
#include <wchar.h>

#include "game_patch.h"
#include "game_ui.h"
#include "ime.h"
#include "input_hook.h"
#include "log.h"
#include "module.h"
#include "plugin_abi.h"
#include "search.h"

typedef int(WINAPI *CSO_FN)(LPCWCH, int, LPCWCH, int, BOOL);

static CSO_FN g_real_cso;
static volatile LONG g_stop;
static volatile LONG g_started;
static volatile LONG g_stopped;
static HANDLE g_stop_event;
static HANDLE g_thread;

static int window_len(LPCWCH s, int limit) {
    int len = 0;
    if (!s) return 0;
    while (len < 255 && s[len]) len++;
    if (limit > 0 && limit < len) len = limit;
    return len;
}

static int WINAPI cso_hook(LPCWCH a, int an, LPCWCH b, int bn, BOOL ignore_case) {
    if (!g_real_cso) return 1;
    if (a && b) {
        int al = window_len(a, an);
        int bl = window_len(b, bn);
        if (al > 0 && bl > 0 && search_hit(a, al, b, bl)) return 2;
    }
    return g_real_cso(a, an, b, bn, ignore_case);
}

static BOOL CALLBACK attach_cb(HWND hwnd, LPARAM param) {
    DWORD pid = 0;
    RECT rect;
    (void)param;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != GetCurrentProcessId() || !IsWindowVisible(hwnd)) return TRUE;
    if (!GetClientRect(hwnd, &rect)) return TRUE;
    if ((rect.right - rect.left) < 320 || (rect.bottom - rect.top) < 200) return TRUE;
    ime_attach(hwnd);
    return TRUE;
}

static void wait_tick(DWORD ms) {
    if (g_stop_event) WaitForSingleObject(g_stop_event, ms);
    else Sleep(ms);
}

static void read_config(int *hooks, int *ime) {
    WCHAR path[MAX_PATH];
    WCHAR *slash;
    HMODULE self = NULL;
    DWORD n;
    *hooks = 1;
    *ime = 1;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCWSTR)&read_config, &self)) {
        return;
    }
    n = GetModuleFileNameW(self, path, MAX_PATH);
    if (!n || n >= MAX_PATH) return;
    slash = wcsrchr(path, L'\\');
    if (!slash) return;
    lstrcpynW(slash + 1, L"pin_yin.ini", (int)(MAX_PATH - (slash + 1 - path)));
    *hooks = GetPrivateProfileIntW(L"Debug", L"Hooks", 1, path);
    *ime = GetPrivateProfileIntW(L"Debug", L"IME", 1, path);
}

static DWORD WINAPI plugin_thread(LPVOID param) {
    int search_patch = 0;
    int input_hooked = 0;
    int last_active = 0;
    int tick = 0;
    int use_hooks;
    int use_ime;
    (void)param;
    log_init();
    read_config(&use_hooks, &use_ime);
    ime_set_enabled(use_ime);
    log_msg("XHL-Remote-Chest-PinYin start hooks=%d ime=%d", use_hooks, use_ime);
    while (!g_stop) {
        int active = ui_available();
        if (active != last_active) {
            last_active = active;
            ime_sync_active(active);
            log_msg("search active %d fg=%p", active, (void *)GetForegroundWindow());
        }
        if (tick <= 0) {
            tick = 30;
            EnumWindows(attach_cb, 0);
            if (use_hooks && !input_hooked) {
                input_hooked = input_hook_install();
            }
            if (module_base()) {
                if (!g_real_cso) {
                    module_iat_patch("CompareStringOrdinal", (void *)cso_hook, (void **)&g_real_cso);
                }
                if (search_patch == 0) {
                    search_patch = patch_search_function() ? 1 : -1;
                }
            }
        }
        tick--;
        wait_tick(16);
    }
    log_msg("XHL-Remote-Chest-PinYin stop");
    return 0;
}

__declspec(dllexport) int XhlNativePluginStart(void *ctx) {
    const XhlPluginStart *start = (const XhlPluginStart *)ctx;
    HANDLE thread;
    if (!xhl_start_valid(ctx)) return 0;
    if ((HMODULE)start->game_module != GetModuleHandleW(NULL)) return 0;
    if (start->process_id != GetCurrentProcessId()) return 0;
    if (InterlockedCompareExchange(&g_started, 1, 0) != 0) return 1;
    g_stop_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    thread = CreateThread(NULL, 0, plugin_thread, NULL, 0, NULL);
    if (!thread) {
        if (g_stop_event) {
            CloseHandle(g_stop_event);
            g_stop_event = NULL;
        }
        InterlockedExchange(&g_started, 0);
        return 0;
    }
    InterlockedExchangePointer((PVOID volatile *)&g_thread, thread);
    return 1;
}

__declspec(dllexport) void XhlNativePluginRequestStop(void) {
    HANDLE thread;
    if (InterlockedExchange(&g_stopped, 1) != 0) return;
    InterlockedExchange(&g_stop, 1);
    if (g_stop_event) SetEvent(g_stop_event);
    thread = (HANDLE)InterlockedExchangePointer((PVOID volatile *)&g_thread, NULL);
    if (thread) {
        WaitForSingleObject(thread, INFINITE);
        CloseHandle(thread);
    }
    ime_detach_all();
    input_hook_uninstall();
    unpatch_search_function();
    module_iat_restore_all();
    if (g_stop_event) {
        CloseHandle(g_stop_event);
        g_stop_event = NULL;
    }
    log_msg("XHL-Remote-Chest-PinYin cleaned up");
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
    }
    return TRUE;
}
