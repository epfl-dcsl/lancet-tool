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
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <strings.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <lancet/agent.h>
#include <lancet/error.h>
#include <lancet/manager.h>
#include <lancet/stats.h>
#include <lancet/timestamping.h>

#define TX_TIMESTAMP_SAMPLING 0.01

static __thread union stats *thread_stats;
static __thread struct timespec prev_tx_timestamp;
static __thread struct tx_samples *tx_s;
static __thread uint32_t tx_sample_selector = 0;

static int configure_stats_shm(void)
{
	int fd, ret;
	void *vaddr;
	char fname[64];

	sprintf(fname, "/lancet-stats%d", get_agent_tid());
	fd = shm_open(fname, O_RDWR | O_CREAT | O_TRUNC, 0660);
	if (fd == -1)
		return 1;

	if (get_agent_type() == THROUGHPUT_AGENT) {
		ret = ftruncate(fd, sizeof(struct throughput_stats) +
								sizeof(struct tx_samples));
		if (ret)
			return ret;

		vaddr = mmap(NULL, sizeof(struct throughput_stats) +
							   sizeof(struct tx_samples),
					 PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
		if (vaddr == MAP_FAILED)
			return 1;

		bzero(vaddr,
			  sizeof(struct throughput_stats) + sizeof(struct tx_samples));

		tx_s = (struct tx_samples *)(((char *)vaddr) +
									 sizeof(struct throughput_stats));
	} else {
		ret = ftruncate(fd, sizeof(struct latency_stats) +
								sizeof(struct tx_samples));
		if (ret)
			return ret;

		vaddr =
			mmap(NULL, sizeof(struct latency_stats) + sizeof(struct tx_samples),
				 PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
		if (vaddr == MAP_FAILED)
			return 1;

		bzero(vaddr, sizeof(struct latency_stats) + sizeof(struct tx_samples));

		tx_s = (struct tx_samples *)(((char *)vaddr) +
									 sizeof(struct latency_stats));
	}

	thread_stats = vaddr;

	return 0;
}

int init_per_thread_stats(void)
{
	int ret;

	ret = configure_stats_shm();
	if (ret)
		return ret;

	return 0;
}

int add_throughput_tx_sample(struct byte_req_pair tx_p)
{
	if (!should_measure())
		return 0;

	thread_stats->th_s.tx.bytes += tx_p.bytes;
	thread_stats->th_s.tx.reqs += tx_p.reqs;

	return 0;
}

int add_throughput_rx_sample(struct byte_req_pair rx_p)
{
	if (!should_measure())
		return 0;

	thread_stats->th_s.rx.bytes += rx_p.bytes;
	thread_stats->th_s.rx.reqs += rx_p.reqs;

	return 0;
}

int add_tx_timestamp(struct timespec *tx_ts)
{
	int res;
	struct timespec *dest;

	if (drand48() < TX_TIMESTAMP_SAMPLING) {
		dest = &tx_s->samples[tx_s->count++ % MAX_PER_THREAD_TX_SAMPLES];
		res = timespec_diff(dest, tx_ts, &prev_tx_timestamp);
		if (res)
			tx_s->count--;
	}

	prev_tx_timestamp = *tx_ts;
	return 0;
}

int add_latency_sample(long diff, struct timespec *tx)
{
	struct lat_sample *lts;

	assert(diff > 0);
	if (!should_measure() ||
		(tx_sample_selector++ % lround(1 / get_sampling_rate())))
		return 0;
	lts = &thread_stats->lt_s
			   .samples[thread_stats->lt_s.inc_idx++ % MAX_PER_THREAD_SAMPLES];
	lts->nsec = diff;
	if (tx)
		lts->tx = *tx;

	return 0;
}
