#pragma once

#include <windows.h>

BYTE *module_base(void);
DWORD module_size(void);
int module_iat_patch(const char *import_name, void *replacement, void **original);
int module_iat_patch_named(const char *module_name, const char *import_name, void *replacement, void **original);
void module_iat_restore_all(void);
