#pragma once

#include <stdio.h>
#include <time.h>

#include "common.h"

#define S_TO_NS 1000000000.0
#define NS_TO_S (1 / 1e9)

static inline u64
gettime_ns()
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
	return ts.tv_sec * S_TO_NS + ts.tv_nsec;
}

static inline string
gettime_ns_str() {
	static char buf[32];
	let t = gettime_ns();
	snprintf(buf, sizeof(buf), "%zu", t);
	return buf;
}

