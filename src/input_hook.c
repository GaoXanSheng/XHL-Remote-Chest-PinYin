#include "input_hook.h"

#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "game_ui.h"
#include "ime.h"
#include "log.h"
#include "module.h"

typedef HRESULT(WINAPI *DI8_CREATE_FN)(HINSTANCE, DWORD, REFIID, LPVOID *, LPUNKNOWN);
typedef HRESULT(STDMETHODCALLTYPE *GET_STATE_FN)(void *, DWORD, void *);
typedef UINT(WINAPI *GET_RAW_FN)(HRAWINPUT, UINT, LPVOID, PUINT, UINT);
typedef SHORT(WINAPI *GET_ASYNC_FN)(int);

typedef struct {
    void **slot;
    void *original;
} VtPatch;

#define MAX_VT_PATCHES 4

static GET_STATE_FN g_real_get_state;
static GET_RAW_FN g_real_get_raw;
static GET_ASYNC_FN g_real_get_async;
static VtPatch g_vt_patches[MAX_VT_PATCHES];
static int g_vt_count;
static int g_installed;
static int g_state_seen;
static DWORD g_last_block_log;

static HRESULT STDMETHODCALLTYPE hook_get_state(void *self, DWORD cb, void *data) {
    if (cb == 256) {
        if (!g_state_seen) {
            g_state_seen = 1;
            log_msg("dinput keyboard state seen");
        }
        if (data && ui_available()) {
            DWORD now = GetTickCount();
            if ((DWORD)(now - g_last_block_log) > 500) {
                g_last_block_log = now;
                log_msg("dinput state block");
            }
            memset(data, 0, cb);
            return 0;
        }
    }
    return g_real_get_state(self, cb, data);
}

static UINT WINAPI hook_get_raw(HRAWINPUT raw_handle, UINT command, LPVOID data, PUINT size, UINT header_size) {
    UINT ret = g_real_get_raw(raw_handle, command, data, size, header_size);
    if (ret != (UINT)-1 && command == RID_INPUT && data && size && *size >= sizeof(RAWINPUTHEADER)) {
        RAWINPUT *raw = (RAWINPUT *)data;
        if (raw->header.dwType == RIM_TYPEKEYBOARD && *size >= sizeof(RAWINPUTHEADER) + sizeof(RAWKEYBOARD)) {
            USHORT vk = raw->data.keyboard.VKey;
            int up = (raw->data.keyboard.Flags & RI_KEY_BREAK) != 0;
            if (vk != 0xFF && ui_available() && !ime_key_allowed(vk)) {
                DWORD now = GetTickCount();
                if ((DWORD)(now - g_last_block_log) > 500) {
                    g_last_block_log = now;
                    log_msg("raw mask vk=%04X up=%d", (unsigned)vk, up);
                }
                /* Map both make and break to an unbound virtual key. Zeroing the
                   whole struct leaves unmatched up events and corrupts the game's
                   keyboard state machine, which only clears on window focus loss. */
                raw->data.keyboard.VKey = 0xFF;
                raw->data.keyboard.MakeCode = 0;
                raw->data.keyboard.Message = up ? WM_KEYUP : WM_KEYDOWN;
                raw->data.keyboard.ExtraInformation = 0;
            }
        }
    }
    return ret;
}

static SHORT WINAPI hook_get_async(int vk) {
    SHORT state = g_real_get_async(vk);
    if (vk < 0x08 || vk > 0xFE) return state;
    if (ui_available() && !ime_key_allowed((WPARAM)vk)) return 0;
    return state;
}

static int install_async_hook(void) {
    int patched = 0;
    if (!g_real_get_async) {
        patched += module_iat_patch_named(NULL, "GetAsyncKeyState", (void *)hook_get_async, (void **)&g_real_get_async);
        patched += module_iat_patch_named("xhl-core.dll", "GetAsyncKeyState", (void *)hook_get_async, (void **)&g_real_get_async);
        patched += module_iat_patch_named("XHL-Remote-Chest.dll", "GetAsyncKeyState", (void *)hook_get_async, (void **)&g_real_get_async);
    }
    if (patched) log_msg("async key hook installed x%d", patched);
    return g_real_get_async != NULL;
}

static int install_raw_hook(void) {
    int patched = 0;
    if (!g_real_get_raw) {
        /* Only the game's own raw input is filtered. The dinput8 chain loader feeds
           the XHL host input manager, which already suspends hotkeys while a text
           field is active; zeroing its keyboard data corrupts that state machine. */
        patched += module_iat_patch_named(NULL, "GetRawInputData", (void *)hook_get_raw, (void **)&g_real_get_raw);
        patched += module_iat_patch_named("xhl-core.dll", "GetRawInputData", (void *)hook_get_raw, (void **)&g_real_get_raw);
        patched += module_iat_patch_named("XHL-Remote-Chest.dll", "GetRawInputData", (void *)hook_get_raw, (void **)&g_real_get_raw);
    }
    if (patched) log_msg("raw input hook installed x%d", patched);
    return g_real_get_raw != NULL;
}

static void patch_slot(void *object, int slot, void *replacement) {
    void **vt = *(void ***)object;
    DWORD old = 0;
    int i;
    if (!vt || vt[slot] == replacement) return;
    for (i = 0; i < g_vt_count; i++) {
        if (g_vt_patches[i].slot == &vt[slot]) return;
    }
    if (g_vt_count >= MAX_VT_PATCHES) return;
    if (!VirtualProtect(&vt[slot], sizeof(void *), PAGE_READWRITE, &old)) return;
    g_real_get_state = (GET_STATE_FN)vt[slot];
    g_vt_patches[g_vt_count].slot = &vt[slot];
    g_vt_patches[g_vt_count].original = vt[slot];
    g_vt_count++;
    vt[slot] = replacement;
    VirtualProtect(&vt[slot], sizeof(void *), old, &old);
    FlushInstructionCache(GetCurrentProcess(), &vt[slot], sizeof(void *));
}

void input_hook_uninstall(void) {
    int i;
    for (i = 0; i < g_vt_count; i++) {
        DWORD old = 0;
        if (!VirtualProtect(g_vt_patches[i].slot, sizeof(void *), PAGE_READWRITE, &old)) continue;
        *g_vt_patches[i].slot = g_vt_patches[i].original;
        VirtualProtect(g_vt_patches[i].slot, sizeof(void *), old, &old);
        FlushInstructionCache(GetCurrentProcess(), g_vt_patches[i].slot, sizeof(void *));
    }
    g_vt_count = 0;
    g_installed = 0;
}

static void install_dinput(void) {
    WCHAR sys[MAX_PATH];
    WCHAR path[MAX_PATH];
    HMODULE dll;
    DI8_CREATE_FN create;
    IDirectInput8W *di = NULL;
    IDirectInputDevice8W *dev = NULL;
    if (!GetSystemDirectoryW(sys, MAX_PATH)) return;
    _snwprintf_s(path, MAX_PATH, _TRUNCATE, L"%s\\dinput8.dll", sys);
    dll = LoadLibraryW(path);
    if (!dll) {
        log_msg("dinput load failed %lu", (unsigned long)GetLastError());
        return;
    }
    create = (DI8_CREATE_FN)GetProcAddress(dll, "DirectInput8Create");
    if (!create) {
        log_msg("dinput create missing");
        return;
    }
    if (FAILED(create(GetModuleHandleW(NULL), DIRECTINPUT_VERSION, &IID_IDirectInput8W, (void **)&di, NULL)) || !di) {
        log_msg("dinput instance failed");
        return;
    }
    if (SUCCEEDED(di->lpVtbl->CreateDevice(di, &GUID_SysKeyboard, &dev, NULL)) && dev) {
        patch_slot(dev, 9, (void *)hook_get_state);
        g_installed = 1;
        log_msg("dinput keyboard hooked");
        dev->lpVtbl->Release(dev);
    } else {
        log_msg("dinput keyboard device failed");
    }
    di->lpVtbl->Release(di);
}

int input_hook_install(void) {
    static int dinput_tried;
    int raw;
    int async;
    if (!g_installed && !dinput_tried) {
        dinput_tried = 1;
        install_dinput();
    }
    raw = install_raw_hook();
    async = install_async_hook();
    return (raw || async) || g_installed;
}
