#pragma once

#include <stdint.h>

#define var __auto_type
#define let var const
#define countof(a) (sizeof(a) / sizeof((a)[0]))

#define forcount(i, n) \
	for (typeof(n + 0) i = 0; i < n; i++)

#define forrange(i, a, b, inc) \
	for (typeof(a + 0) i = a, i##_end = b; i < i##_end; i += inc)

typedef unsigned uint;
typedef uint64_t u64;
typedef const char *string;

