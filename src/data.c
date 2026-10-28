#include "data.h"

#include <string.h>

#include "item_table.h"
#include "pinyin_table.h"

static unsigned long long name_hash(const char *s, int n) {
    unsigned long long h = 1469598103934665603ULL;
    int i;
    for (i = 0; i < n; i++) {
        h ^= (unsigned char)s[i];
        h *= 1099511628211ULL;
    }
    return h;
}

int data_item_count(void) {
    return ITEM_N;
}

int data_find_item(const char *s, int n) {
    unsigned long long h;
    unsigned int lo = 0;
    unsigned int hi = ITEM_N;
    if (!s || n <= 0) return -1;
    h = name_hash(s, n);
    while (lo < hi) {
        unsigned int mid = lo + (hi - lo) / 2;
        if (ITEM_HASH[mid] < h) lo = mid + 1;
        else hi = mid;
    }
    for (; lo < ITEM_N && ITEM_HASH[lo] == h; lo++) {
        int idx = ITEM_IDX[lo];
        if (ITEM_NAME_LEN[idx] == n && memcmp(NAME_POOL + ITEM_NAME_OFF[idx], s, (size_t)n) == 0) return idx;
    }
    return -1;
}

static int ci_eq(const char *a, const char *b, int n) {
    int i;
    for (i = 0; i < n; i++) {
        unsigned char x = (unsigned char)a[i];
        unsigned char y = (unsigned char)b[i];
        if (x >= 'A' && x <= 'Z') x = (unsigned char)(x + 32);
        if (y >= 'A' && y <= 'Z') y = (unsigned char)(y + 32);
        if (x != y) return 0;
    }
    return 1;
}

int data_token_hit(const char *tok, int tl, int idx) {
    int i;
    if (!tok || tl <= 0 || idx < 0 || idx >= ITEM_N) return 0;
    for (i = 0; i < TOKEN_N; i++) {
        int hit = 0;
        if (TOKEN_LEN[i] >= tl && memcmp(TOKEN_POOL + TOKEN_OFF[i], tok, (size_t)tl) == 0) hit = 1;
        if (!hit && ALIAS_LEN[i] >= tl && ci_eq((const char *)ALIAS_POOL + ALIAS_OFF[i], tok, tl)) hit = 1;
        if (!hit) continue;
        if (TOKEN_KIND[i] == 0 && ITEM_CAT[idx] == TOKEN_ID[i]) return 1;
        if (TOKEN_KIND[i] == 1 && ITEM_TYPE[idx] == TOKEN_ID[i]) return 1;
        if (TOKEN_KIND[i] == 2 && ITEM_SUB[idx] == TOKEN_ID[i]) return 1;
    }
    if (ITEM_DESC_LEN[idx] > 0) {
        const char *d = (const char *)DESC_POOL + ITEM_DESC_OFF[idx];
        int dl = ITEM_DESC_LEN[idx];
        for (i = 0; i + tl <= dl; i++) {
            if (memcmp(d + i, tok, (size_t)tl) == 0) return 1;
        }
    }
    return 0;
}

int data_py_lookup(wchar_t c, const char **readings, int *lengths, int max) {
    unsigned int index;
    const char *p;
    int count;
    int k = 0;
    if (c < PY_FIRST || c > PY_LAST || max <= 0) return 0;
    index = (unsigned int)(c - PY_FIRST);
    count = PY_CNT[index];
    p = (const char *)PY_POOL + PY_OFF[index];
    while (k < count && k < max) {
        size_t len = strlen(p);
        readings[k] = p;
        lengths[k] = (int)len;
        k++;
        p += len + 1;
    }
    return k;
}
