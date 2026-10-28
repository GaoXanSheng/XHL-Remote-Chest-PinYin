#pragma once

#include <windows.h>

int ime_attach(HWND hwnd);
void ime_set_enabled(int enabled);
void ime_sync_active(int active);
void ime_detach_all(void);
int ime_key_allowed(WPARAM vk);
