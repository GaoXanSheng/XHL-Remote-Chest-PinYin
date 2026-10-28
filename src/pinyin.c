#include "pinyin.h"

#include <string.h>

#include "data.h"

#define MAX_QUERY 127
#define MAX_NAME 255
#define MAX_READINGS 8
#define MAX_VARIANTS 6
#define MEMO_BYTES (((MAX_NAME + 1) * (MAX_QUERY + 1) + 7) / 8)

typedef struct {
    char buf[MAX_VARIANTS][8];
    const char *ptr[MAX_VARIANTS];
    int len[MAX_VARIANTS];
    int n;
} Variants;

static int ascii_lower(int c) {
    return (c >= 'A' && c <= 'Z') ? c + 32 : c;
}

static int is_query_sep(int c) {
    return c == ' ' || c == '-' || c == '\'' || c == '_';
}

static int is_name_sep(wchar_t c) {
    return c == L' ' || c == L'-' || c == L'\'' || c == L'_' || c == (wchar_t)0x00B7;
}

static int literal_eq(wchar_t name_char, int query_char) {
    if (name_char < 0x80) return ascii_lower((int)name_char) == query_char;
    return 0;
}

static int common_prefix(const char *p, int plen, const char *q, int qn) {
    int i = 0;
    while (i < plen && i < qn && p[i] == q[i]) i++;
    return i;
}

static int add_variant(Variants *v, const char *p, int len) {
    int i;
    for (i = 0; i < v->n; i++) {
        if (v->len[i] == len && memcmp(v->ptr[i], p, (size_t)len) == 0) return 1;
    }
    if (v->n >= MAX_VARIANTS || len > 7) return 0;
    memcpy(v->buf[v->n], p, (size_t)len);
    v->buf[v->n][len] = 0;
    v->ptr[v->n] = v->buf[v->n];
    v->len[v->n] = len;
    v->n++;
    return 1;
}

static void variants_of(const char *p, int len, int role, const PyOptions *opt, Variants *v) {
    char tmp[8];
    v->n = 0;
    add_variant(v, p, len);
    if (role == 0 && opt->fuzzy_initial) {
        if (len == 2 && p[1] == 'h' && (p[0] == 'z' || p[0] == 'c' || p[0] == 's')) {
            add_variant(v, p, 1);
        } else if (len == 1 && (p[0] == 'z' || p[0] == 'c' || p[0] == 's')) {
            tmp[0] = p[0];
            tmp[1] = 'h';
            tmp[2] = 0;
            add_variant(v, tmp, 2);
        }
    }
    if (role == 1 && opt->fuzzy_final) {
        if (len == 3 && (memcmp(p, "ang", 3) == 0 || memcmp(p, "eng", 3) == 0 || memcmp(p, "ing", 3) == 0)) {
            add_variant(v, p, 2);
        } else if (len == 2 && p[1] == 'n' && (p[0] == 'a' || p[0] == 'e' || p[0] == 'i')) {
            tmp[0] = p[0];
            tmp[1] = 'n';
            tmp[2] = 'g';
            tmp[3] = 0;
            add_variant(v, tmp, 3);
        }
    }
    if (opt->fuzzy_v && len > 0 && len <= 7 && p[0] == 'v') {
        tmp[0] = 'u';
        memcpy(tmp + 1, p + 1, (size_t)(len - 1));
        add_variant(v, tmp, len);
    }
}

static int consume_phoneme(const char *p, int len, int role, const PyOptions *opt,
                           const unsigned char *in, unsigned char *out,
                           const char *q, int qn) {
    Variants v;
    int pos;
    int k;
    int any = 0;
    variants_of(p, len, role, opt, &v);
    memset(out, 0, MAX_QUERY + 1);
    for (pos = 0; pos <= qn; pos++) {
        if (!in[pos]) continue;
        if (pos == qn) {
            out[qn] = 1;
            any = 1;
            continue;
        }
        for (k = 0; k < v.n; k++) {
            int c = common_prefix(v.ptr[k], v.len[k], q + pos, qn - pos);
            if (c == v.len[k]) {
                out[pos + c] = 1;
                any = 1;
            } else if (c > 0 && pos + c == qn) {
                out[qn] = 1;
                any = 1;
            }
        }
    }
    return any;
}

static int reading_positions(const char *r, int rlen, const PyOptions *opt,
                             int start, unsigned char *out, const char *q, int qn) {
    unsigned char cur[MAX_QUERY + 1];
    unsigned char next[MAX_QUERY + 1];
    int cursor = 0;
    int i;
    if (rlen <= 0) return 0;
    memset(cur, 0, sizeof(cur));
    cur[start] = 1;
    if (!(r[0] == 'a' || r[0] == 'e' || r[0] == 'i' || r[0] == 'o' || r[0] == 'u' || r[0] == 'v')) {
        cursor = (rlen > 2 && r[1] == 'h') ? 2 : 1;
        if (cursor > rlen) cursor = rlen;
    }
    if (cursor > 0) {
        if (!consume_phoneme(r, cursor, 0, opt, cur, next, q, qn)) return 0;
        memcpy(cur, next, sizeof(cur));
    }
    if (cursor < rlen) {
        if (!consume_phoneme(r + cursor, rlen - cursor, 1, opt, cur, next, q, qn)) return 0;
        memcpy(cur, next, sizeof(cur));
    }
    for (i = 0; i <= qn; i++) {
        if (cur[i]) out[i] = 1;
    }
    return 1;
}

static int match_seq(const wchar_t *name, int n, int i, const char *q, int qn, int j,
                     const PyOptions *opt, unsigned char *memo) {
    unsigned char bits[MAX_QUERY + 1];
    const char *readings[MAX_READINGS];
    int lengths[MAX_READINGS];
    size_t bit;
    int count;
    int k;
    while (j < qn && is_query_sep((unsigned char)q[j])) j++;
    if (j >= qn) return 1;
    bit = (size_t)i * (size_t)(qn + 1) + (size_t)j;
    if (memo[bit >> 3] & (unsigned char)(1u << (bit & 7))) return 0;
    for (;;) {
        if (i >= n) goto miss;
        if (is_name_sep(name[i])) {
            i++;
            continue;
        }
        if (name[i] < 0x80) {
            wchar_t c = name[i];
            if ((c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z')) break;
            i++;
            continue;
        }
        if (data_py_lookup(name[i], readings, lengths, 1) > 0) break;
        i++;
    }
    memset(bits, 0, sizeof(bits));
    if (literal_eq(name[i], (unsigned char)q[j])) bits[j + 1] = 1;
    count = data_py_lookup(name[i], readings, lengths, MAX_READINGS);
    if (qn == 1 && count > 1) count = 1;
    for (k = 0; k < count; k++) {
        unsigned char out[MAX_QUERY + 1];
        int pos;
        memset(out, 0, sizeof(out));
        reading_positions(readings[k], lengths[k], opt, j, out, q, qn);
        for (pos = j; pos <= qn; pos++) {
            if (out[pos]) bits[pos] = 1;
        }
        if (q[j] == readings[k][0]) bits[j + 1] = 1;
    }
    for (k = j; k <= qn; k++) {
        if (bits[k] && match_seq(name, n, i + 1, q, qn, k, opt, memo)) return 1;
    }
miss:
    memo[bit >> 3] |= (unsigned char)(1u << (bit & 7));
    return 0;
}

int py_contains(const wchar_t *name, int n, const char *q, int qn, const PyOptions *opt) {
    unsigned char memo[MEMO_BYTES];
    size_t total;
    int i;
    if (!name || !q || !opt || n <= 0 || qn <= 0) return 0;
    if (n > MAX_NAME) n = MAX_NAME;
    if (qn > MAX_QUERY) qn = MAX_QUERY;
    total = (size_t)(n + 1) * (size_t)(qn + 1);
    memset(memo, 0, (total + 7) / 8);
    for (i = 0; i < n; i++) {
        if (match_seq(name, n, i, q, qn, 0, opt, memo)) return 1;
    }
    return 0;
}
