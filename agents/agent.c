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
#define _GNU_SOURCE
#include <fcntl.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <sys/types.h>

#include <lancet/agent.h>
#include <lancet/app_proto.h>
#include <lancet/error.h>
#include <lancet/stats.h>
#include <lancet/timestamping.h>
#include <lancet/tp_proto.h>

static struct agent_config *cfg;
static struct agent_control_block *acb;
static __thread struct request to_send;
static __thread struct iovec received;
static __thread int thread_idx;
pthread_barrier_t conn_open_barrier;

int should_load(void)
{
	return acb->should_load;
}

int should_measure(void)
{
	return acb->should_measure;
}

int get_conn_count(void)
{
	return cfg->conn_count;
}

int get_thread_count(void)
{
	return cfg->thread_count;
}

int get_target_count(void)
{
	return cfg->target_count;
}

struct application_protocol * get_app_proto(void)
{
        return cfg->app_proto;
}

struct host_tuple *get_targets(void)
{
	return cfg->targets;
}

long get_ia(void)
{
	return lround(generate(cfg->idist) * 1000);
}

enum agent_type get_agent_type(void)
{
	return cfg->atype;
}

int get_agent_tid(void)
{
	return thread_idx;
}

uint32_t get_per_thread_samples(void)
{
	return acb->per_thread_samples;
}

double get_sampling_rate(void)
{
	return acb->sampling;
}

char *get_if_name(void)
{
	return cfg->if_name;
}

int get_max_pending_reqs(void)
{
	return cfg->per_conn_reqs;
}

void set_conn_open(int val)
{
	acb->conn_open = val;
}

struct request *prepare_request(void)
{
	create_request(cfg->app_proto, &to_send);

	return &to_send;
}

struct byte_req_pair process_response(char *buf, int size)
{
	received.iov_base = buf;
	received.iov_len = size;
	return consume_response(cfg->app_proto, &received);
}

static void* agent_main(void *arg)
{
	cpu_set_t cpuset;
	pthread_t thread;
	int s;

	thread = pthread_self();
	thread_idx = (int)(long)arg;
	init_per_thread_stats();

	srand(time(NULL) + thread_idx * 12345);

	CPU_ZERO(&cpuset);
	CPU_SET(thread_idx, &cpuset);

	s = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
	if (s != 0) {
		char buf[256];
		snprintf(buf, sizeof(buf), "ERROR: pthread_setaffinity_np failed for thread %d", thread_idx);
		lancet_perror(buf);
		return NULL;
	}
	cfg->tp->tp_main[cfg->atype]();

	return NULL;
}

static int configure_control_block(void)
{
	int fd, ret;
	void *vaddr;

	fd = shm_open("/lancetcontrol", O_RDWR | O_CREAT | O_TRUNC, 0660);
	if (fd == -1)
		return 1;

	ret = ftruncate(fd, sizeof(struct agent_control_block));
	if (ret)
		return ret;

	vaddr = mmap(NULL, sizeof(struct agent_control_block),
				 PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (vaddr == MAP_FAILED)
		return 1;

	acb = vaddr;

	bzero((void *)acb, sizeof(struct agent_control_block));
	acb->thread_count = get_thread_count();
	acb->idist = *cfg->idist;
	free(cfg->idist);
	cfg->idist = &acb->idist;
	acb->agent_type = get_agent_type();

	return 0;
}

int main(int argc, char **argv)
{
	int i;
	pthread_t *tids;

	cfg = parse_arguments(argc, argv);
	if (!cfg)
		exit(-1);

	if (cfg->atype == SYMMETRIC_NIC_TIMESTAMP_AGENT)
		enable_nic_timestamping(cfg->if_name);

	/* Auto-select number of threads to use */
	if (cfg->thread_count == -1) {
		cfg->thread_count = get_nprocs();
		if (cfg->conn_count < cfg->thread_count) {
			cfg->thread_count = cfg->conn_count;
			lancet_fprintf(stderr, "Limiting number of threads to %d\n", cfg->thread_count);
		}
	}

	if (configure_control_block()) {
		lancet_fprintf(stderr, "failed to init the control block\n");
		exit(-1);
	}

	tids = malloc(cfg->thread_count * sizeof(pthread_t));
	if (!tids) {
		lancet_fprintf(stderr, "Failed to allocate tids\n");
		exit(-1);
	}

	int ret = pthread_barrier_init(&conn_open_barrier, NULL, cfg->thread_count);
	assert(!ret);

	for (i = 1; i < cfg->thread_count; i++) {
		if (pthread_create(&tids[i], NULL, agent_main, (void *)(long)i)) {
			lancet_fprintf(stderr, "failed to spawn thread %d\n", i);
			exit(-1);
		}
	}

	agent_main(0);

	return 0;
}
