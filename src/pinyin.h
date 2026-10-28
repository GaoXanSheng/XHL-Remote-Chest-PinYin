#pragma once

#include <wchar.h>

typedef struct {
    int fuzzy_initial;
    int fuzzy_final;
    int fuzzy_v;
} PyOptions;

int py_contains(const wchar_t *name, int n, const char *query, int qn, const PyOptions *opt);
