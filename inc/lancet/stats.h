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

#include <assert.h>
#include <stdint.h>

#include <lancet/rand_gen.h>

#define MAX_PER_THREAD_SAMPLES 131072
#define MAX_PER_THREAD_TX_SAMPLES 4096

struct byte_req_pair {
	uint64_t bytes;
	uint64_t reqs;
};

struct __attribute__((packed)) tx_samples {
	uint32_t count;
	struct timespec samples[MAX_PER_THREAD_SAMPLES];
};

struct __attribute__((packed)) throughput_stats {
	struct byte_req_pair rx;
	struct byte_req_pair tx;
};

struct __attribute__((packed)) lat_sample {
	uint64_t nsec;
	struct timespec tx; // used for iid-ness checks
};

struct __attribute__((packed)) latency_stats {
	struct throughput_stats th_s;
	uint32_t inc_idx; // increment idx
	struct lat_sample samples[MAX_PER_THREAD_SAMPLES];
};

union stats {
	struct throughput_stats th_s;
	struct latency_stats lt_s;
};

int init_per_thread_stats(void);
int add_throughput_tx_sample(struct byte_req_pair tx_p);
int add_throughput_rx_sample(struct byte_req_pair rx_p);
int add_tx_timestamp(struct timespec *tx_ts);
int add_latency_sample(long diff, struct timespec *tx);
// void clear_stats(union stats *stats);
// void compute_latency_percentiles(struct latency_stats *lt_s);
// void compute_latency_percentiles_ci(struct latency_stats *lt_s);
// void set_per_thread_samples(int samples, double sr);
// uint32_t compute_convergence(struct lat_sample *samples, int size);
// void clear_all_stats(void);
// void aggregate_throughput_stats(union stats *agg_stats);
// void aggregate_latency_samples(union stats *agg_stats);
// int check_ia(void);
// void collect_reference_ia(struct rand_gen *gen);
// void init_reference_ia_dist(struct rand_gen *gen);
// void set_reference_load(uint32_t load);
// double check_iid(struct latency_stats *lt_s);
