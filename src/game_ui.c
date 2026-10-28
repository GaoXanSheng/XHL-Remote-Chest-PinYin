#include "game_ui.h"

#include <windows.h>
#include <string.h>

#include "log.h"
#include "module.h"

#define UI_TEXT_OFF 0x922B0u
#define UI_DIRTY_OFF 0x923B0u
#define UI_ACTIVE_OFF 0x92501u
#define UI_TEXT_CAP 256

static int g_state;
static BYTE *g_text;
static BYTE *g_dirty;
static BYTE *g_active;

static int region_ok(BYTE *base, DWORD size, uintptr_t off, size_t len, int writable) {
    MEMORY_BASIC_INFORMATION info;
    BYTE *p;
    if (off >= size || len > (size_t)(size - off)) return 0;
    p = base + off;
    if (VirtualQuery(p, &info, sizeof(info)) == 0) return 0;
    if (info.State != MEM_COMMIT) return 0;
    if (info.Protect & PAGE_GUARD) return 0;
    if (writable) {
        DWORD prot = info.Protect & 0xFF;
        if (prot != PAGE_READWRITE && prot != PAGE_WRITECOPY && prot != PAGE_EXECUTE_READWRITE && prot != PAGE_EXECUTE_WRITECOPY) return 0;
    } else {
        DWORD prot = info.Protect & 0xFF;
        if (prot == PAGE_NOACCESS || prot == PAGE_EXECUTE) return 0;
    }
    return (BYTE *)info.BaseAddress + info.RegionSize >= p + len;
}

static int ready(void) {
    BYTE *base;
    DWORD size;
    if (g_state) return g_state > 0;
    base = module_base();
    size = module_size();
    if (!base) return 0;
    if (!size || !region_ok(base, size, UI_TEXT_OFF, UI_TEXT_CAP, 1) ||
        !region_ok(base, size, UI_DIRTY_OFF, 1, 1) || !region_ok(base, size, UI_ACTIVE_OFF, 1, 0)) {
        g_state = -1;
        log_msg("ui buffers unavailable");
        return 0;
    }
    g_text = base + UI_TEXT_OFF;
    g_dirty = base + UI_DIRTY_OFF;
    g_active = base + UI_ACTIVE_OFF;
    g_state = 1;
    log_msg("ui buffers verified text=%p active=%p", (void *)g_text, (void *)g_active);
    return 1;
}

int ui_available(void) {
    if (!ready()) return 0;
    return *g_active != 0;
}

int ui_text(char *out, int cap) {
    int len;
    if (!out || cap <= 0) return 0;
    out[0] = 0;
    if (!ui_available()) return 0;
    len = 0;
    while (len < UI_TEXT_CAP - 1 && g_text[len]) len++;
    if (len > cap - 1) len = cap - 1;
    memcpy(out, g_text, (size_t)len);
    out[len] = 0;
    return len;
}

int ui_set_text(const char *text, int len) {
    int limit;
    if (!text || len < 0) return 0;
    if (!ui_available()) return 0;
    limit = UI_TEXT_CAP - 1;
    if (len > limit) {
        len = limit;
        while (len > 0 && ((unsigned char)text[len] & 0xC0) == 0x80) len--;
    }
    memcpy(g_text, text, (size_t)len);
    g_text[len] = 0;
    *g_dirty = 1;
    return len;
}
