#pragma once

#include <stdint.h>

#define XHL_PLUGIN_QUERY_SIZE 0xEC
#define XHL_PLUGIN_ABI_VERSION 0x10000
#define XHL_PLUGIN_START_SIZE 0x28
#define XHL_PLUGIN_START_VERSION 0x10000

typedef struct {
    uint32_t size;
    uint32_t abi;
    uint32_t flags;
    char id[16];
    unsigned char reserved1[0x30];
    char name[16];
    unsigned char reserved2[0x70];
    char version[32];
} XhlPluginQuery;

typedef struct {
    uint32_t size;
    uint32_t version;
    uint64_t game_module;
    uint64_t plugin_module;
    uint32_t process_id;
    uint32_t reserved;
    const void *api;
} XhlPluginStart;

int xhl_query_fill(void *info);
int xhl_start_valid(const void *ctx);
__declspec(dllexport) int XhlNativePluginQuery(void *info);
