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
#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <time.h>

#include <lancet/agent.h>
#include <lancet/error.h>
#include <lancet/misc.h>
#include <lancet/tp_proto.h>

#include <lancet/timestamping.h>
#include <r2p2/api-internal.h>
#include <r2p2/api.h>

void lancet_success_nic_timestamping_cb(long handle, void *arg,
										struct iovec *iov, int iovcnt)
{
#ifdef R2P2_NIC_TS
	struct r2p2_ctx *ctx;
	struct timespec latency;
	int ret;

	// FIXME: Need to process response
	struct byte_req_pair read_res = {0};

	for (int i = 0; i < iovcnt; i++)
		read_res.bytes += iov[i].iov_len;
	read_res.reqs = 1;
	add_throughput_rx_sample(read_res);

	ctx = (struct r2p2_ctx *)arg;
	ret = timespec_diff(&latency, &ctx->rx_timestamp, &ctx->tx_timestamp);

	if (ret == 0) {
		add_tx_timestamp(&ctx->tx_timestamp);
		add_latency_sample(latency.tv_nsec + latency.tv_sec * 1e9,
						   &ctx->tx_timestamp);
	}

	// free ctx
	free(arg);

	r2p2_recv_resp_done(handle);
#endif
}

void lancet_success_timestamping_cb(long handle, void *arg, struct iovec *iov,
									int iovcnt)
{
	struct timespec rx_timestamp, latency;
	struct timespec *tx_timestamp;
	struct r2p2_ctx *ctx;
	int ret;

	time_ns_to_ts(&rx_timestamp);

	// FIXME: Need to process response
	struct byte_req_pair read_res = {0};

	for (int i = 0; i < iovcnt; i++)
		read_res.bytes += iov[i].iov_len;
	read_res.reqs = 1;
	add_throughput_rx_sample(read_res);

	ctx = (struct r2p2_ctx *)arg;
	tx_timestamp = (struct timespec *)(ctx + 1);
	ret = timespec_diff(&latency, &rx_timestamp, tx_timestamp);
	if (ret == 0) {
		add_latency_sample(latency.tv_nsec + latency.tv_sec * 1e9,
						   tx_timestamp);
	}

	// free ctx
	free(arg);

	r2p2_recv_resp_done(handle);
}

void lancet_success_cb(long handle, void *arg, struct iovec *iov, int iovcnt)
{
	// FIXME: Need to process response
	struct byte_req_pair read_res;

	for (int i = 0; i < iovcnt; i++)
		read_res.bytes += iov[i].iov_len;
	read_res.reqs = 1;
	add_throughput_rx_sample(read_res);

	// free ctx
	free(arg);

	r2p2_recv_resp_done(handle);
}

void lancet_error_cb(void *arg, int err)
{
	free(arg);
}

void lancet_timeout_cb(void *arg)
{
	free(arg);
}

static void throughput_r2p2_main(void)
{
	long next_tx, diff;
	struct r2p2_ctx *ctx;
	struct request *to_send;
	struct r2p2_host_tuple *targets;
	int target_count;
	struct byte_req_pair send_res;
	struct timespec tx_timestamp;

	if (r2p2_init_per_core(get_agent_tid(), get_thread_count())) {
		lancet_fprintf(stderr, "Error initialising per core\n");
		return;
	}

	targets = (struct r2p2_host_tuple *)get_targets();
	target_count = get_target_count();
	next_tx = time_ns();
	while (1) {
		if (!should_load()) {
			next_tx = time_ns();
			continue;
		}
		diff = time_ns() - next_tx;
		if (diff >= 0) {
			// prepare msg to be sent
			to_send = prepare_request();
			// prepare ctx
			ctx = malloc(sizeof(struct r2p2_ctx));
			assert(ctx);
			ctx->success_cb = lancet_success_cb;
			ctx->error_cb = lancet_error_cb;
			ctx->timeout_cb = lancet_timeout_cb;
			if (to_send->meta == (void *)FIXED_ROUTE)
				ctx->destination = &targets[0];
			else
				ctx->destination = &targets[rand() % target_count];
			ctx->arg = (void *)ctx;
			ctx->timeout = 5000000;
			ctx->routing_policy = (int)(unsigned long)to_send->meta;

			time_ns_to_ts(&tx_timestamp);
			add_tx_timestamp(&tx_timestamp);

			// send msg
			r2p2_send_req(&to_send->iovs[0], 1, ctx);

			/* Bookkeeping */
			send_res.bytes = to_send->iovs[0].iov_len;
			send_res.reqs = 1;
			add_throughput_tx_sample(send_res);

			// schedule next
			next_tx += get_ia();
		}

		// poll for responses
		r2p2_poll();
	}
	return;
}

static int *create_polling_sockets(void)
{
	int per_thread_conn, s, ret, busy_wait = 5000;
	int *sockets;
	struct timeval tv;

	per_thread_conn = get_conn_count() / get_thread_count();

	sockets = malloc(per_thread_conn * sizeof(int));
	assert(sockets);

	tv.tv_sec = 2;
	tv.tv_usec = 0;

	for (int i = 0; i < per_thread_conn; i++) {
		s = socket(AF_INET, SOCK_DGRAM, 0);
		assert(s);

		// Enable busy polling
		ret = setsockopt(s, SOL_SOCKET, SO_BUSY_POLL, &busy_wait,
						 sizeof(busy_wait));
		if (ret) {
			lancet_perror("Error setsockopt SO_BUSY_POLL");
			return NULL;
		}
		ret = setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (struct timeval *)&tv,
						 sizeof(struct timeval));
		if (ret) {
			lancet_perror("Error setsockopt SO_RCVTIMEO");
			return NULL;
		}
		sockets[i] = s;
	}
	return sockets;
}

static void latency_r2p2_main(void)
{
	long next_tx, diff, start_time, end_time;
	struct sockaddr_in server;
	int *sockets;
	struct request *to_send;
	struct r2p2_msg msg;
	uint16_t rid;
	struct r2p2_host_tuple *targets, *target;
	generic_buffer gb;
	char *payload, *buf;
	int policy, per_thread_conn, s, target_count, byte_count, payload_len, ret,
		len = sizeof(server), bufsize;
	struct r2p2_header *r2p2h;
	struct byte_req_pair brp;

	if (r2p2_init_per_core(get_agent_tid(), get_thread_count())) {
		lancet_fprintf(stderr, "Error initialising per core\n");
		return;
	}

	per_thread_conn = get_conn_count() / get_thread_count();
	targets = (struct r2p2_host_tuple *)get_targets();
	target_count = get_target_count();
	bufsize = 1500; // sizeof(long) + sizeof(struct r2p2_header);
	buf = malloc(bufsize);

	sockets = create_polling_sockets();
	assert(sockets);

	next_tx = time_ns();
	while (1) {
		if (!should_load()) {
			next_tx = time_ns();
			continue;
		}
		diff = time_ns() - next_tx;
		if (diff < 0)
			continue;

		// Prepare request
		to_send = prepare_request();
		bzero(&msg, sizeof(struct r2p2_msg));
		policy = (int)(unsigned long)to_send->meta;
		rid = rand();
		r2p2_prepare_msg(&msg, &to_send->iovs[0], 1, REQUEST_MSG, policy, rid);
		gb = msg.head_buffer;

		// randomly pick conn
		s = sockets[rand() % per_thread_conn];

		// Configure target
		if (policy == FIXED_ROUTE)
			target = &targets[0];
		else
			target = &targets[rand() % target_count];
		server.sin_family = AF_INET;
		server.sin_port = htons(target->port);
		server.sin_addr.s_addr = target->ip;

		// start_time = rdtsc();
		start_time = time_ns();
		// send msg
		byte_count = 0;
		while (gb) {
			payload = get_buffer_payload(gb);
			payload_len = get_buffer_payload_size(gb);
			ret = sendto(s, payload, payload_len, 0, (struct sockaddr *)&server,
						 len);
			assert(ret == payload_len);
			byte_count += ret;
			gb = get_buffer_next(gb);
		}

		/*BookKeeping*/
		brp.bytes = to_send->iovs[0].iov_len;
		brp.reqs = 1;
		add_throughput_tx_sample(brp);

		byte_count = 0;
		// block
		while (1) {
			ret = read(s, buf, bufsize);
			if (ret != -1) {
				byte_count += ret;
				r2p2h = (struct r2p2_header *)buf;
				if (rid != r2p2h->rid) {
					lancet_fprintf(stderr, "Wrong id");
					break;
				}
				if (is_last(r2p2h)) {
					break;
				}
			} else {
				// lancet_fprintf(stderr, "Timeout in: %u:%d\n", target->ip,
				// target->port);
				break;
			}
		}
		// FIXME: should process reply
		// end_time = rdtsc();
		end_time = time_ns();
		// bookkeeping
		add_latency_sample(end_time - start_time, NULL);
		brp.bytes = byte_count - sizeof(struct r2p2_header);
		brp.reqs = 1;
		add_throughput_rx_sample(brp);

		// free msg
		gb = msg.head_buffer;
		while (gb) {
			free_buffer(gb);
			gb = get_buffer_next(gb);
		}

		// schedule next
		next_tx += get_ia();
	}
	return;
}

static void symmetric_nic_r2p2_main(void)
{
#ifdef R2P2_NIC_TS
	long next_tx, diff;
	struct r2p2_ctx *ctx;
	struct request *to_send;
	struct r2p2_host_tuple *targets;
	int target_count;
	struct byte_req_pair send_res;

	if (r2p2_init_per_core(get_agent_tid(), get_thread_count())) {
		lancet_fprintf(stderr, "Error initialising per core\n");
		return;
	}

	targets = (struct r2p2_host_tuple *)get_targets();
	target_count = get_target_count();
	next_tx = time_ns();
	while (1) {
		if (!should_load()) {
			next_tx = time_ns();
			continue;
		}
		diff = time_ns() - next_tx;
		if (diff >= 0) {
			// prepare msg to be sent
			to_send = prepare_request();
			// prepare ctx
			ctx = malloc(sizeof(struct r2p2_ctx));
			assert(ctx);
			ctx->success_cb = lancet_success_nic_timestamping_cb;
			ctx->error_cb = lancet_error_cb;
			ctx->timeout_cb = lancet_timeout_cb;

			// Important, set default values to 0 or calloc when using NIC
			// timestamp!
			ctx->rx_timestamp.tv_sec = 0;
			ctx->rx_timestamp.tv_nsec = 0;
			ctx->tx_timestamp.tv_sec = 0;
			ctx->tx_timestamp.tv_nsec = 0;

			if (to_send->meta)
				ctx->destination = &targets[0];
			else
				ctx->destination = &targets[rand() % target_count];
			ctx->arg = (void *)ctx;
			ctx->timeout = 5000000;
			ctx->routing_policy = (int)(unsigned long)to_send->meta;

			// send msg
			r2p2_send_req(&to_send->iovs[0], 1, ctx);

			/* Bookkeeping */
			send_res.bytes = to_send->iovs[0].iov_len;
			send_res.reqs = 1;
			add_throughput_tx_sample(send_res);

			// schedule next
			next_tx += get_ia();
		}

		// poll for responses
		r2p2_poll();
	}
#endif
}

static void symmetric_r2p2_main(void)
{
	long next_tx, diff;
	struct r2p2_ctx *ctx;
	struct request *to_send;
	struct r2p2_host_tuple *targets;
	int target_count;
	struct byte_req_pair send_res;
	struct timespec *tx_timestamp;

	if (r2p2_init_per_core(get_agent_tid(), get_thread_count())) {
		lancet_fprintf(stderr, "Error initialising per core\n");
		return;
	}

	targets = (struct r2p2_host_tuple *)get_targets();
	target_count = get_target_count();
	next_tx = time_ns();
	while (1) {
		if (!should_load()) {
			next_tx = time_ns();
			continue;
		}
		diff = time_ns() - next_tx;
		if (diff >= 0) {
			// prepare msg to be sent
			to_send = prepare_request();
			// prepare ctx
			ctx = (struct r2p2_ctx *)malloc(sizeof(struct r2p2_ctx) +
											sizeof(struct timespec));
			assert(ctx);
			bzero(ctx, sizeof(struct r2p2_ctx) + sizeof(struct timespec));
			tx_timestamp = (struct timespec *)(ctx + 1);
			ctx->success_cb = lancet_success_timestamping_cb;
			ctx->error_cb = lancet_error_cb;
			ctx->timeout_cb = lancet_timeout_cb;
			if (to_send->meta == (void *)FIXED_ROUTE)
				ctx->destination = &targets[0];
			else
				ctx->destination = &targets[rand() % target_count];
			time_ns_to_ts(tx_timestamp);
			ctx->arg = (void *)ctx;
			ctx->timeout = 5000000;
			ctx->routing_policy = (int)(unsigned long)to_send->meta;

			// send msg
			r2p2_send_req(&to_send->iovs[0], 1, ctx);

			/* Bookkeeping */
			send_res.bytes = to_send->iovs[0].iov_len;
			send_res.reqs = 1;
			add_throughput_tx_sample(send_res);
			add_tx_timestamp(tx_timestamp);

			// schedule next
			next_tx += get_ia();
		}

		// poll for responses
		r2p2_poll();
	}
	return;
}

struct transport_protocol *init_r2p2(void)
{
	struct transport_protocol *tp;

	tp = malloc(sizeof(struct transport_protocol));
	if (!tp) {
		lancet_fprintf(stderr, "Failed to alloc transport_protocol\n");
		return NULL;
	}

	tp->tp_main[THROUGHPUT_AGENT] = throughput_r2p2_main;
	tp->tp_main[LATENCY_AGENT] = latency_r2p2_main;
	tp->tp_main[SYMMETRIC_NIC_TIMESTAMP_AGENT] = symmetric_nic_r2p2_main;
	tp->tp_main[SYMMETRIC_AGENT] = symmetric_r2p2_main;

	if (r2p2_init(8000)) {
		lancet_fprintf(stderr, "Failed to init r2p2\n");
		return NULL;
	}

	return tp;
}
