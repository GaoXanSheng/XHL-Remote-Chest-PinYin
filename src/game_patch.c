#include "game_patch.h"

#include <string.h>
#include <windows.h>

#include "log.h"
#include "module.h"

typedef struct {
    DWORD sig_rva;
    const unsigned char *sig;
    int sig_len;
    int patch_off;
    const unsigned char *patch;
    int patch_len;
    const char *name;
} PatchSite;

static const unsigned char SIG_BRANCH_HEAP[] = {0x4C, 0x39, 0xC0, 0x72, 0x18, 0xEB, 0x2D};
static const unsigned char PATCH_BRANCH_HEAP[] = {0xEB, 0x2F};
static const unsigned char SIG_BRANCH_SSO[] = {0x4C, 0x39, 0xC0, 0x73, 0x17, 0x48, 0xFF};
static const unsigned char PATCH_BRANCH_SSO[] = {0xEB, 0x17};
static const unsigned char SIG_BOUND[] = {0x48, 0x29, 0xD1, 0x49, 0xFF, 0xC4, 0x49, 0x39, 0xCC, 0x77, 0x8F};
static const unsigned char PATCH_BOUND[] = {0x90, 0x90, 0x90};

static const PatchSite SITES[] = {
    {0x88C2, SIG_BRANCH_HEAP, sizeof(SIG_BRANCH_HEAP), 3, PATCH_BRANCH_HEAP, sizeof(PATCH_BRANCH_HEAP), "branch-heap"},
    {0x88DA, SIG_BRANCH_SSO, sizeof(SIG_BRANCH_SSO), 3, PATCH_BRANCH_SSO, sizeof(PATCH_BRANCH_SSO), "branch-sso"},
    {0x8945, SIG_BOUND, sizeof(SIG_BOUND), 0, PATCH_BOUND, sizeof(PATCH_BOUND), "window-bound"},
};

static int restore_site(BYTE *base, const PatchSite *site) {
    BYTE *p = base + site->sig_rva + site->patch_off;
    DWORD old_protect = 0;
    if (memcmp(p, site->patch, (size_t)site->patch_len) != 0) return 0;
    if (!VirtualProtect(p, (SIZE_T)site->patch_len, PAGE_EXECUTE_READWRITE, &old_protect)) return 0;
    memcpy(p, site->sig + site->patch_off, (size_t)site->patch_len);
    VirtualProtect(p, (SIZE_T)site->patch_len, old_protect, &old_protect);
    FlushInstructionCache(GetCurrentProcess(), p, (SIZE_T)site->patch_len);
    return 1;
}

void unpatch_search_function(void) {
    BYTE *base = module_base();
    DWORD size = module_size();
    int count = (int)(sizeof(SITES) / sizeof(SITES[0]));
    int restored = 0;
    int i;
    if (!base || !size) return;
    for (i = 0; i < count; i++) {
        const PatchSite *site = &SITES[i];
        if ((DWORD)site->sig_rva + (DWORD)site->patch_off + (DWORD)site->patch_len > size) continue;
        restored += restore_site(base, site);
    }
    if (restored) log_msg("search patches restored x%d", restored);
}

int patch_search_function(void) {
    BYTE *base = module_base();
    DWORD size = module_size();
    int count = (int)(sizeof(SITES) / sizeof(SITES[0]));
    int i;

    if (!base || !size) return 0;
    for (i = 0; i < count; i++) {
        const PatchSite *site = &SITES[i];
        BYTE *p = base + site->sig_rva;
        if (site->sig_rva + site->sig_len > size) {
            log_msg("search patch %s out of range", site->name);
            return 0;
        }
        if (memcmp(p, site->sig, (size_t)site->sig_len) != 0) {
            log_msg("search patch %s signature mismatch", site->name);
            return 0;
        }
    }
    for (i = 0; i < count; i++) {
        const PatchSite *site = &SITES[i];
        BYTE *p = base + site->sig_rva + site->patch_off;
        DWORD old_protect = 0;
        if (!VirtualProtect(p, (SIZE_T)site->patch_len, PAGE_EXECUTE_READWRITE, &old_protect)) {
            int j;
            log_msg("search patch %s protect failed %lu", site->name, (unsigned long)GetLastError());
            for (j = 0; j < i; j++) restore_site(base, &SITES[j]);
            return 0;
        }
        memcpy(p, site->patch, (size_t)site->patch_len);
        VirtualProtect(p, (SIZE_T)site->patch_len, old_protect, &old_protect);
        FlushInstructionCache(GetCurrentProcess(), p, (SIZE_T)site->patch_len);
        log_msg("search patch %s applied", site->name);
    }
    return 1;
}
