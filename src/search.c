#include "search.h"

#include <windows.h>
#include <string.h>
#include <wctype.h>

#include "data.h"
#include "pinyin.h"

static const PyOptions DEFAULT_OPTIONS = {1, 1, 1};

static int is_ascii_query(const wchar_t *q, int n) {
    int i;
    int alnum = 0;
    for (i = 0; i < n; i++) {
        wchar_t c = q[i];
        if ((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9')) alnum = 1;
        else if (c != L' ' && c != L'-' && c != L'\'' && c != L'_') return 0;
    }
    return alnum;
}

static int has_cjk(const wchar_t *s, int n) {
    int i;
    for (i = 0; i < n; i++) {
        if (s[i] >= 0x3400 && s[i] <= 0x9FFF) return 1;
        if (s[i] >= 0xF900 && s[i] <= 0xFAFF) return 1;
    }
    return 0;
}

static int token_hit(const wchar_t *name, int n, const wchar_t *tok, int tn) {
    char name8[1024];
    char tok8[1024];
    int nn;
    int tt;
    int idx;
    if (tn <= 0 || n <= 0) return 0;
    if (n > 255) n = 255;
    if (tn > 255) tn = 255;
    nn = WideCharToMultiByte(CP_UTF8, 0, name, n, name8, sizeof(name8) - 1, NULL, NULL);
    if (nn <= 0) return 0;
    name8[nn] = 0;
    tt = WideCharToMultiByte(CP_UTF8, 0, tok, tn, tok8, sizeof(tok8) - 1, NULL, NULL);
    if (tt <= 0) return 0;
    tok8[tt] = 0;
    idx = data_find_item(name8, nn);
    if (idx < 0) return 0;
    return data_token_hit(tok8, tt, idx);
}

static int pinyin_hit(const wchar_t *name, int n, const wchar_t *query, int qn) {
    char q[128];
    int qi = 0;
    int i;
    if (n <= 0 || qn <= 0) return 0;
    if (n > 255) n = 255;
    if (!has_cjk(name, n)) return 0;
    for (i = 0; i < qn; i++) {
        wchar_t c = query[i];
        if (c >= L'A' && c <= L'Z') c = (wchar_t)(c + 32);
        if ((c >= L'a' && c <= L'z') || (c >= L'0' && c <= L'9')) {
            if (qi >= (int)sizeof(q) - 1) return 0;
            q[qi++] = (char)c;
        }
    }
    if (qi == 0) return 0;
    return py_contains(name, n, q, qi, &DEFAULT_OPTIONS);
}

static int literal_contains(const wchar_t *name, int n, const wchar_t *query, int qn) {
    int i;
    int j;
    if (!name || !query || n <= 0 || qn <= 0 || qn > n) return 0;
    for (i = 0; i + qn <= n; i++) {
        for (j = 0; j < qn; j++) {
            if (towlower(name[i + j]) != towlower(query[j])) break;
        }
        if (j == qn) return 1;
    }
    return 0;
}

int search_hit(const wchar_t *a, int an, const wchar_t *b, int bn) {
    if (!a || !b || an <= 0 || bn <= 0) return 0;
    if (b[0] == L'#') return token_hit(a, an, b + 1, bn - 1);
    if (a[0] == L'#') return token_hit(b, bn, a + 1, an - 1);
    if (is_ascii_query(b, bn) && !is_ascii_query(a, an)) {
        if (pinyin_hit(a, an, b, bn)) return 1;
    } else if (is_ascii_query(a, an) && !is_ascii_query(b, bn)) {
        if (pinyin_hit(b, bn, a, an)) return 1;
    }
    return literal_contains(a, an, b, bn);
}
