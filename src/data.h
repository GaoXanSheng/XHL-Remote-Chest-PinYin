#pragma once

#include <wchar.h>

int data_item_count(void);
int data_find_item(const char *name, int len);
int data_token_hit(const char *token, int token_len, int item_index);
int data_py_lookup(wchar_t c, const char **readings, int *lengths, int max);
