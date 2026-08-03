#pragma once

#include <stdbool.h>

void hcf(void);
void panic(const char *msg);
void assert_impl(const char *function, int line, bool condition, const char *condition_str, const char *msg);

#define ASSERT(x, ...) assert_impl(__func__, __LINE__, (x), #x, "" __VA_ARGS__)
