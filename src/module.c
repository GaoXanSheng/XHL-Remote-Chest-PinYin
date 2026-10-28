#include "module.h"

#include <string.h>

#include "log.h"

#define MAX_IAT_PATCHES 16

typedef struct {
    ULONG_PTR *slot;
    ULONG_PTR original;
    BYTE *base;
    DWORD size;
} IatPatch;

static BYTE *g_base;
static DWORD g_size;
static IatPatch g_patches[MAX_IAT_PATCHES];
static int g_patch_count;
static SRWLOCK g_patch_lock = SRWLOCK_INIT;

static IMAGE_NT_HEADERS *nt_headers(BYTE *base, DWORD *size) {
    IMAGE_DOS_HEADER *dos;
    IMAGE_NT_HEADERS *nt;
    if (!base) return NULL;
    dos = (IMAGE_DOS_HEADER *)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return NULL;
    nt = (IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return NULL;
    if (size) *size = nt->OptionalHeader.SizeOfImage;
    return nt;
}

BYTE *module_base(void) {
    HMODULE handle;
    if (g_base) return g_base;
    handle = GetModuleHandleA("XHL-Remote-Chest.dll");
    if (!handle) return NULL;
    if (!nt_headers((BYTE *)handle, &g_size)) {
        log_msg("module headers invalid");
        return NULL;
    }
    g_base = (BYTE *)handle;
    log_msg("module base %p size %lX", (void *)g_base, (unsigned long)g_size);
    return g_base;
}

DWORD module_size(void) {
    return g_size;
}

static int in_image(const BYTE *base, DWORD size, const void *ptr, size_t len) {
    const BYTE *p = (const BYTE *)ptr;
    size_t offset;
    if (p < base) return 0;
    offset = (size_t)(p - base);
    if (offset > (size_t)size) return 0;
    return len <= (size_t)size - offset;
}

static int import_name_eq(const BYTE *base, DWORD size, DWORD rva, const char *target) {
    size_t i = 0;
    if (rva >= size) return 0;
    while ((size_t)rva + i < (size_t)size) {
        char c = (char)base[rva + i];
        if (c != target[i]) return 0;
        if (!c) return 1;
        i++;
    }
    return 0;
}

static void record_patch(BYTE *base, DWORD size, ULONG_PTR *slot, ULONG_PTR original) {
    int i;
    AcquireSRWLockExclusive(&g_patch_lock);
    for (i = 0; i < g_patch_count; i++) {
        if (g_patches[i].slot == slot) break;
    }
    if (i == g_patch_count && g_patch_count < MAX_IAT_PATCHES) {
        IatPatch *p = &g_patches[g_patch_count++];
        p->slot = slot;
        p->original = original;
        p->base = base;
        p->size = size;
    }
    ReleaseSRWLockExclusive(&g_patch_lock);
}

void module_iat_restore_all(void) {
    int i;
    int count;
    int restored = 0;
    AcquireSRWLockExclusive(&g_patch_lock);
    count = g_patch_count;
    g_patch_count = 0;
    for (i = 0; i < count; i++) {
        IatPatch *p = &g_patches[i];
        DWORD old_protect = 0;
        if (!in_image(p->base, p->size, p->slot, sizeof(ULONG_PTR))) continue;
        if (!VirtualProtect(p->slot, sizeof(ULONG_PTR), PAGE_READWRITE, &old_protect)) continue;
        *p->slot = p->original;
        VirtualProtect(p->slot, sizeof(ULONG_PTR), old_protect, &old_protect);
        FlushInstructionCache(GetCurrentProcess(), p->slot, sizeof(ULONG_PTR));
        restored++;
    }
    ReleaseSRWLockExclusive(&g_patch_lock);
    if (restored) log_msg("restored %d iat patch(es)", restored);
}

static int patch_iat(BYTE *base, DWORD size, const char *import_name, void *replacement, void **original) {
    IMAGE_NT_HEADERS *nt;
    DWORD import_rva;
    DWORD import_size;
    IMAGE_IMPORT_DESCRIPTOR *imp;
    int patched = 0;
    if (!base || !size || !import_name || !replacement || !original) return 0;
    nt = nt_headers(base, NULL);
    if (!nt) return 0;
    import_rva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    import_size = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size;
    if (!import_rva || !import_size || import_rva >= size) return 0;
    imp = (IMAGE_IMPORT_DESCRIPTOR *)(base + import_rva);
    for (; in_image(base, size, imp, sizeof(*imp)) && imp->Name; imp++) {
        IMAGE_THUNK_DATA *orig;
        IMAGE_THUNK_DATA *iat;
        DWORD thunk_rva = imp->OriginalFirstThunk ? imp->OriginalFirstThunk : imp->FirstThunk;
        if (imp->Name >= size || !thunk_rva || thunk_rva >= size) continue;
        if (!imp->FirstThunk || imp->FirstThunk >= size) continue;
        orig = (IMAGE_THUNK_DATA *)(base + thunk_rva);
        iat = (IMAGE_THUNK_DATA *)(base + imp->FirstThunk);
        for (; in_image(base, size, orig, sizeof(*orig)) && orig->u1.AddressOfData; orig++, iat++) {
            ULONG_PTR previous;
            DWORD old_protect = 0;
            if (!in_image(base, size, iat, sizeof(*iat))) break;
            if (orig->u1.Ordinal & IMAGE_ORDINAL_FLAG) continue;
            if (orig->u1.AddressOfData >= size) break;
            /* AddressOfData points at IMAGE_IMPORT_BY_NAME, whose Name follows a WORD Hint */
            if (!import_name_eq(base, size, (DWORD)(orig->u1.AddressOfData + 2), import_name)) continue;
            if (iat->u1.Function == (ULONG_PTR)replacement) continue;
            previous = iat->u1.Function;
            if (!*original) *original = (void *)previous;
            if (!VirtualProtect(&iat->u1.Function, sizeof(void *), PAGE_READWRITE, &old_protect)) continue;
            iat->u1.Function = (ULONG_PTR)replacement;
            VirtualProtect(&iat->u1.Function, sizeof(void *), old_protect, &old_protect);
            record_patch(base, size, &iat->u1.Function, previous);
            patched++;
        }
    }
    if (patched) log_msg("patched %s x%d original %p", import_name, patched, *original);
    return patched;
}

int module_iat_patch(const char *import_name, void *replacement, void **original) {
    return patch_iat(module_base(), module_size(), import_name, replacement, original);
}

int module_iat_patch_named(const char *module_name, const char *import_name, void *replacement, void **original) {
    HMODULE handle = GetModuleHandleA(module_name);
    DWORD size = 0;
    if (!handle) return 0;
    if (!nt_headers((BYTE *)handle, &size)) return 0;
    return patch_iat((BYTE *)handle, size, import_name, replacement, original);
}
