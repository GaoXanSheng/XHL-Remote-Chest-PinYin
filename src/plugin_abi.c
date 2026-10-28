#include "plugin_abi.h"

#include <string.h>

_Static_assert(sizeof(XhlPluginQuery) == XHL_PLUGIN_QUERY_SIZE, "query struct size");
_Static_assert(sizeof(XhlPluginStart) == XHL_PLUGIN_START_SIZE, "start struct size");

static void set_field(char *dst, size_t cap, const char *src) {
    size_t n = strlen(src);
    if (n >= cap) n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = 0;
}

int xhl_query_fill(void *info) {
    XhlPluginQuery *q = (XhlPluginQuery *)info;
    if (!q || q->size != XHL_PLUGIN_QUERY_SIZE) return 0;
    memset(q->id, 0, sizeof(q->id));
    memset(q->reserved1, 0, sizeof(q->reserved1));
    memset(q->name, 0, sizeof(q->name));
    memset(q->reserved2, 0, sizeof(q->reserved2));
    memset(q->version, 0, sizeof(q->version));
    q->abi = XHL_PLUGIN_ABI_VERSION;
    q->flags = 0;
    set_field(q->id, sizeof(q->id), "xhl-rc-pinyin");
    set_field(q->name, sizeof(q->name), "Remote Chest CN");
    set_field(q->version, sizeof(q->version), "1.0.0");
    return 1;
}

int xhl_start_valid(const void *ctx) {
    const XhlPluginStart *s = (const XhlPluginStart *)ctx;
    if (!s || s->size != XHL_PLUGIN_START_SIZE) return 0;
    if (s->version != XHL_PLUGIN_START_VERSION) return 0;
    if (!s->api) return 0;
    return 1;
}

int XhlNativePluginQuery(void *info) {
    return xhl_query_fill(info);
}
