/*
 * MIT License
 *
 * Copyright (c) 2019-2021 Ecole Polytechnique Federale Lausanne (EPFL)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#pragma once

#include <sys/time.h>

static inline long time_us(void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (long)tv.tv_sec * 1000000 + (long)tv.tv_usec;
}

static inline int64_t time_ns()
{
	struct timespec ts;
	int r = clock_gettime(CLOCK_MONOTONIC, &ts);
	assert(r == 0);
	return (ts.tv_nsec + ts.tv_sec * 1e9);
}

static inline void time_ns_to_ts(struct timespec *ts)
{
	int r = clock_gettime(CLOCK_MONOTONIC, ts);
	assert(r == 0);
}

static inline unsigned long rdtsc(void)
{
	unsigned int a, d;
	asm volatile("rdtsc" : "=a"(a), "=d"(d));
	return ((unsigned long)a) | (((unsigned long)d) << 32);
}
