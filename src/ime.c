#include "ime.h"

#include <imm.h>
#include <string.h>

#include "game_ui.h"
#include "log.h"

#define MAX_TEXT 256
#define MAX_SLOTS 8
#define MAX_RECENT 16
#define WM_XHL_IME_ACTIVE (WM_APP + 0x51)

typedef struct {
    HWND hwnd;
    WNDPROC old;
} WndSlot;

static WndSlot g_slots[MAX_SLOTS];
static SRWLOCK g_slot_lock = SRWLOCK_INIT;
static int g_ime_enabled = 1;
static char g_base[MAX_TEXT];
static int g_base_len;
static int g_base_valid;
static DWORD g_last_commit;
static WCHAR g_recent[MAX_RECENT];
static DWORD g_recent_tick[MAX_RECENT];
static int g_recent_pos;

static WNDPROC old_proc(HWND hwnd) {
    WNDPROC old = NULL;
    int i;
    AcquireSRWLockShared(&g_slot_lock);
    for (i = 0; i < MAX_SLOTS; i++) {
        if (g_slots[i].hwnd == hwnd) {
            old = g_slots[i].old;
            break;
        }
    }
    ReleaseSRWLockShared(&g_slot_lock);
    return old;
}

static int recent_hit(WCHAR c, DWORD ms) {
    DWORD now = GetTickCount();
    int i;
    for (i = 0; i < MAX_RECENT; i++) {
        if (g_recent[i] == c && g_recent_tick[i] && (DWORD)(now - g_recent_tick[i]) < ms) return 1;
    }
    return 0;
}

static void recent_add(const WCHAR *text, int n) {
    DWORD now = GetTickCount();
    int i;
    for (i = 0; i < n; i++) {
        g_recent[g_recent_pos] = text[i];
        g_recent_tick[g_recent_pos] = now;
        g_recent_pos = (g_recent_pos + 1) % MAX_RECENT;
    }
}

static int trim_ascii_tail(char *buf, int len) {
    while (len > 0) {
        unsigned char c = (unsigned char)buf[len - 1];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '\'' || c == ' ') len--;
        else break;
    }
    return len;
}

static void apply_suffix(const char *suffix, int sn, int use_base, int trim) {
    char buf[1024];
    int n;
    if (!suffix || sn < 0) return;
    if (use_base && g_base_valid) {
        n = g_base_len;
        if (n > (int)sizeof(buf) - 1) n = (int)sizeof(buf) - 1;
        memcpy(buf, g_base, (size_t)n);
    } else {
        n = ui_text(buf, (int)sizeof(buf));
        if (trim) n = trim_ascii_tail(buf, n);
    }
    if (sn > (int)sizeof(buf) - 1 - n) sn = (int)sizeof(buf) - 1 - n;
    if (sn > 0) memcpy(buf + n, suffix, (size_t)sn);
    ui_set_text(buf, n + sn);
}

static void apply_wchars(const WCHAR *text, int n, int use_base, int trim) {
    char u8[MAX_TEXT * 4];
    int m;
    if (!text || n <= 0) return;
    if (n > MAX_TEXT - 1) n = MAX_TEXT - 1;
    m = WideCharToMultiByte(CP_UTF8, 0, text, n, u8, sizeof(u8) - 1, NULL, NULL);
    if (m <= 0) return;
    apply_suffix(u8, m, use_base, trim);
}

static int get_composition(HIMC hc, DWORD type, WCHAR *out, int cap) {
    LONG bytes;
    if (cap <= 1) return 0;
    bytes = ImmGetCompositionStringW(hc, type, NULL, 0);
    if (bytes <= 0) return 0;
    if (bytes > (LONG)(cap - 1) * 2) bytes = (LONG)(cap - 1) * 2;
    bytes = ImmGetCompositionStringW(hc, type, out, (DWORD)bytes);
    if (bytes < 0) return 0;
    return (int)(bytes / 2);
}

int ime_key_allowed(WPARAM vk) {
    if (GetKeyState(VK_CONTROL) & 0x8000) return 1;
    if (GetKeyState(VK_MENU) & 0x8000) return 1;
    switch (vk) {
    case VK_BACK:
    case VK_DELETE:
    case VK_LEFT:
    case VK_RIGHT:
    case VK_UP:
    case VK_DOWN:
    case VK_HOME:
    case VK_END:
    case VK_RETURN:
    case VK_ESCAPE:
    case VK_TAB:
    case VK_SHIFT:
    case VK_LSHIFT:
    case VK_RSHIFT:
    case VK_CONTROL:
    case VK_LCONTROL:
    case VK_RCONTROL:
    case VK_MENU:
    case VK_LMENU:
    case VK_RMENU:
    case VK_CAPITAL:
    case VK_NUMLOCK:
    case VK_SCROLL:
    case VK_PROCESSKEY:
    case VK_PACKET:
        return 1;
    default:
        return 0;
    }
}

static LRESULT CALLBACK ime_active_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, WNDPROC old);

void ime_set_enabled(int enabled) {
    g_ime_enabled = enabled ? 1 : 0;
}

static void cancel_composition(HWND hwnd) {
    HIMC hc = ImmGetContext(hwnd);
    HIMC previous;
    int open = -1;
    if (hc) {
        open = ImmGetOpenStatus(hc) ? 1 : 0;
        ImmNotifyIME(hc, NI_COMPOSITIONSTR, CPS_CANCEL, 0);
        ImmReleaseContext(hwnd, hc);
    }
    previous = ImmAssociateContext(hwnd, NULL);
    log_msg("ime off %p hc=%p prev=%p open=%d", (void *)hwnd, (void *)hc, (void *)previous, open);
}

static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == WM_XHL_IME_ACTIVE) {
        if (!g_ime_enabled) return 0;
        if (wparam) {
            HIMC hc;
            if (!ImmAssociateContextEx(hwnd, NULL, IACE_DEFAULT)) {
                log_msg("ime associate failed %p err %lu", (void *)hwnd, (unsigned long)GetLastError());
            }
            hc = ImmGetContext(hwnd);
            log_msg("ime on %p hc=%p open=%d", (void *)hwnd, (void *)hc, hc ? (ImmGetOpenStatus(hc) ? 1 : 0) : -1);
            if (hc) ImmReleaseContext(hwnd, hc);
        } else {
            cancel_composition(hwnd);
        }
        return 0;
    }
    {
        WNDPROC old = old_proc(hwnd);
        if (!old) return DefWindowProcW(hwnd, msg, wparam, lparam);
        if (!ui_available()) {
            if (msg == WM_IME_STARTCOMPOSITION || msg == WM_IME_COMPOSITION ||
                msg == WM_IME_ENDCOMPOSITION || msg == WM_IME_CHAR) {
                /* IME left open from a finished edit session: release it on the
                   first key it steals instead of staying locked until deactivation */
                log_msg("ime stolen %p msg=%04X", (void *)hwnd, (unsigned)msg);
                cancel_composition(hwnd);
                return 0;
            }
            return CallWindowProcW(old, hwnd, msg, wparam, lparam);
        }
        return ime_active_proc(hwnd, msg, wparam, lparam, old);
    }
}

static LRESULT CALLBACK ime_active_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, WNDPROC old) {
    if (msg == WM_IME_SETCONTEXT) {
        lparam &= ~(LPARAM)(ULONG_PTR)ISC_SHOWUICOMPOSITIONWINDOW;
        return CallWindowProcW(old, hwnd, msg, wparam, lparam);
    }
    switch (msg) {
    case WM_IME_STARTCOMPOSITION:
        g_base_len = ui_text(g_base, MAX_TEXT);
        g_base_valid = 1;
        log_msg("ime start base=%d", g_base_len);
        return 0;
    case WM_IME_COMPOSITION: {
        HIMC hc = ImmGetContext(hwnd);
        int handled = 0;
        if (hc) {
            if (lparam & GCS_RESULTSTR) {
                WCHAR result[MAX_TEXT];
                int n = get_composition(hc, GCS_RESULTSTR, result, MAX_TEXT);
                if (n > 0) {
                    apply_wchars(result, n, 1, 0);
                    recent_add(result, n);
                    g_base_valid = 0;
                    g_last_commit = GetTickCount();
                    log_msg("ime commit %d", n);
                }
                handled = 1;
            } else if (lparam & GCS_COMPSTR) {
                WCHAR comp[MAX_TEXT];
                int n = get_composition(hc, GCS_COMPSTR, comp, MAX_TEXT);
                if (!g_base_valid) {
                    g_base_len = ui_text(g_base, MAX_TEXT);
                    g_base_valid = 1;
                }
                if (n > 0) {
                    apply_wchars(comp, n, 1, 0);
                } else {
                    apply_suffix("", 0, 1, 0);
                }
                handled = 1;
            }
            ImmReleaseContext(hwnd, hc);
        }
        if (handled) return 0;
        break;
    }
    case WM_IME_ENDCOMPOSITION:
        if (g_base_valid) {
            ui_set_text(g_base, g_base_len);
            g_base_valid = 0;
        }
        return 0;
    case WM_IME_CHAR: {
        WCHAR c = (WCHAR)wparam;
        if ((DWORD)(GetTickCount() - g_last_commit) >= 50 && !recent_hit(c, 150)) {
            apply_wchars(&c, 1, 0, 1);
            recent_add(&c, 1);
            log_msg("ime char %04X", (unsigned)c);
        }
        return 0;
    }
    case WM_CHAR:
        if (wparam >= 0x80) {
            WCHAR c = (WCHAR)wparam;
            if (!recent_hit(c, 150)) {
                apply_wchars(&c, 1, 0, 1);
                recent_add(&c, 1);
                log_msg("wm char %04X", (unsigned)c);
            }
            return 0;
        }
        break;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYUP:
        /* Action keys are filtered at the Raw Input layer; letting key messages
           reach the game keeps its text-field state machine consistent */
        break;
    default:
        break;
    }
    return CallWindowProcW(old, hwnd, msg, wparam, lparam);
}

/* -2 = already attached, -1 = no free slot, otherwise the slot index */
static int find_slot(HWND hwnd) {
    int i;
    int free_slot = -1;
    for (i = 0; i < MAX_SLOTS; i++) {
        if (g_slots[i].hwnd == hwnd) return -2;
        if (g_slots[i].hwnd && !IsWindow(g_slots[i].hwnd)) {
            g_slots[i].hwnd = NULL;
            g_slots[i].old = NULL;
        }
        if (!g_slots[i].hwnd && free_slot < 0) free_slot = i;
    }
    return free_slot;
}

int ime_attach(HWND hwnd) {
    WNDPROC current;
    WNDPROC previous;
    int slot;
    if (!hwnd || !IsWindow(hwnd)) return 0;
    AcquireSRWLockExclusive(&g_slot_lock);
    slot = find_slot(hwnd);
    ReleaseSRWLockExclusive(&g_slot_lock);
    if (slot == -2) return 1;
    if (slot < 0) return 0;
    current = (WNDPROC)GetWindowLongPtrW(hwnd, GWLP_WNDPROC);
    if (!current) return 0;
    AcquireSRWLockExclusive(&g_slot_lock);
    g_slots[slot].hwnd = hwnd;
    g_slots[slot].old = current;
    SetLastError(0);
    previous = (WNDPROC)SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)wndproc);
    if (!previous && GetLastError() != 0) {
        g_slots[slot].hwnd = NULL;
        g_slots[slot].old = NULL;
        ReleaseSRWLockExclusive(&g_slot_lock);
        log_msg("subclass failed %p err %lu", (void *)hwnd, (unsigned long)GetLastError());
        return 0;
    }
    if (previous != current) g_slots[slot].old = previous;
    ReleaseSRWLockExclusive(&g_slot_lock);
    log_msg("subclass %p old %p", (void *)hwnd, (void *)previous);
    if (g_ime_enabled) {
        PostMessageW(hwnd, WM_XHL_IME_ACTIVE, (WPARAM)(ui_available() ? 1 : 0), 0);
    }
    return 1;
}

void ime_sync_active(int active) {
    HWND windows[MAX_SLOTS];
    int count = 0;
    int i;
    if (!g_ime_enabled) return;
    AcquireSRWLockShared(&g_slot_lock);
    for (i = 0; i < MAX_SLOTS; i++) {
        if (g_slots[i].hwnd && g_slots[i].old && IsWindow(g_slots[i].hwnd)) {
            windows[count++] = g_slots[i].hwnd;
        }
    }
    ReleaseSRWLockShared(&g_slot_lock);
    for (i = 0; i < count; i++) {
        PostMessageW(windows[i], WM_XHL_IME_ACTIVE, (WPARAM)(active ? 1 : 0), 0);
    }
    if (count) log_msg("ime mode %d", active);
}

void ime_detach_all(void) {
    HWND windows[MAX_SLOTS];
    WNDPROC olds[MAX_SLOTS];
    int count = 0;
    int i;
    AcquireSRWLockExclusive(&g_slot_lock);
    for (i = 0; i < MAX_SLOTS; i++) {
        if (!g_slots[i].hwnd) continue;
        windows[count] = g_slots[i].hwnd;
        olds[count] = g_slots[i].old;
        count++;
    }
    ReleaseSRWLockExclusive(&g_slot_lock);
    for (i = 0; i < count; i++) {
        DWORD_PTR result = 0;
        if (!IsWindow(windows[i])) continue;
        SendMessageTimeoutW(windows[i], WM_XHL_IME_ACTIVE, 0, 0, SMTO_ABORTIFHUNG, 500, &result);
        if (olds[i]) SetWindowLongPtrW(windows[i], GWLP_WNDPROC, (LONG_PTR)olds[i]);
    }
    AcquireSRWLockExclusive(&g_slot_lock);
    for (i = 0; i < MAX_SLOTS; i++) {
        g_slots[i].hwnd = NULL;
        g_slots[i].old = NULL;
    }
    ReleaseSRWLockExclusive(&g_slot_lock);
}
