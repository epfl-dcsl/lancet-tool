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
#include <assert.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <netdb.h>
#include <netinet/in.h>

#include <assert.h>
#include <string.h>
#include <sys/uio.h>

#include <lancet/agent.h>
#include <lancet/coord_proto.h>
#include <lancet/error.h>
#include <lancet/manager.h>
#include <lancet/misc.h>
#include <lancet/stats.h>

static volatile int agents_should_load;
static volatile int agents_should_measure;
static long start_measure_time;
static long stop_measure_time;
static union stats *agg_stats;

int should_load(void)
{
	return agents_should_load;
}

int should_measure(void)
{
	return agents_should_measure;
}

int manager_init(int thread_count)
{
	agg_stats = malloc(sizeof(union stats) +
					   AGG_SAMPLE_SIZE * sizeof(struct lat_sample));
	assert(agg_stats);

	return 0;
}

static int create_socket(void)
{
	int sockfd, portno;
	struct sockaddr_in serv_addr;

	sockfd = socket(AF_INET, SOCK_STREAM, 0);

	if (sockfd < 0) {
		lancet_perror("ERROR opening socket");
		return -1;
	}

	bzero((char *)&serv_addr, sizeof(serv_addr));
	portno = MANAGER_PORT;

	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = INADDR_ANY;
	serv_addr.sin_port = htons(portno);

	if (bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
		lancet_perror("ERROR on binding");
		return -1;
	}

	listen(sockfd, 1);
	return sockfd;
}

static int accept_conn(int fd)
{
	int clilen, sockfd;
	struct sockaddr_in cli_addr;
	struct linger linger;

	clilen = sizeof(cli_addr);
	sockfd = accept(fd, (struct sockaddr *)&cli_addr, (socklen_t *)&clilen);

	/* Close with RST not FIN */
	linger.l_onoff = 1;
	linger.l_linger = 0;
	if (setsockopt(sockfd, SOL_SOCKET, SO_LINGER, (void *)&linger,
				   sizeof(linger))) {
		perror("setsockopt(SO_LINGER)");
		exit(1);
	}
	if (sockfd < 0) {
		lancet_perror("ERROR on accept");
		return -1;
	}
	return sockfd;
}

/*
 * Collectes latency samples in 2 halves for KS test
 */
static void collect_latency_stats(void)
{
	aggregate_latency_samples(agg_stats);
	//#ifndef SINGLE_REQ
	compute_latency_percentiles_ci(&agg_stats->lt_s);
	//#endif
}

static void reply_throughput_stats(int sockfd)
{
	struct iovec iov[4];
	long duration;
	struct throughput_reply data;
	int n, iovcnt, to_send;
	struct msg1 m1;
	struct msg2 m2;

	duration = stop_measure_time - start_measure_time;
	aggregate_throughput_stats(agg_stats);

	m1.Hdr.MessageType = REPLY;
	m1.Hdr.MessageLength = sizeof(struct throughput_reply) + sizeof(uint32_t);
	m1.Info = REPLY_STATS_THROUGHPUT;

	data.Rx_bytes = agg_stats->th_s.rx.bytes;
	data.Tx_bytes = agg_stats->th_s.tx.bytes;
	data.Req_count = agg_stats->th_s.rx.reqs;
	data.Duration = duration;
	iovcnt = 2;
	iov[0].iov_base = &m1;
	iov[0].iov_len = sizeof(struct msg1);
	iov[1].iov_base = &data;
	iov[1].iov_len = sizeof(struct throughput_reply);
	to_send = sizeof(struct msg1) + sizeof(struct throughput_reply);

	if (get_agent_type() == SYMMETRIC_NIC_TIMESTAMP_AGENT) {
		m2.Hdr.MessageType = REPLY;
		m2.Hdr.MessageLength = 2 * sizeof(uint32_t);
		m2.Info1 = REPLY_IA_COMP;
		m2.Info2 = check_ia();

		iov[2].iov_base = &m2;
		iov[2].iov_len = sizeof(struct msg2);
		iovcnt = 3;
		to_send += sizeof(struct msg2);
	}

	n = writev(sockfd, iov, iovcnt);
	assert(n == to_send);
}

static void reply_latency_stats(int sockfd)
{
	struct iovec iov[7];
	long duration;
	struct latency_reply data;
	int n, iovcnt, to_send;
	struct msg1 m, m1, m2;
	struct msg2 m3;
	double pearson_corr;
	uint32_t conv;

	duration = stop_measure_time - start_measure_time;
	collect_latency_stats();

	m.Hdr.MessageType = REPLY;
	m.Hdr.MessageLength = sizeof(struct throughput_reply);
	m.Info = REPLY_STATS_LATENCY;

	data.Th_data.Rx_bytes = agg_stats->lt_s.th_s.rx.bytes;
	data.Th_data.Tx_bytes = agg_stats->lt_s.th_s.tx.bytes;
	data.Th_data.Req_count = agg_stats->lt_s.th_s.rx.reqs;
	data.Th_data.Duration = duration;
	data.Avg_lat = agg_stats->lt_s.avg_lat;
	data.P50_i = agg_stats->lt_s.p50_i;
	data.P50 = agg_stats->lt_s.p50;
	data.P50_k = agg_stats->lt_s.p50_k;
	data.P90_i = agg_stats->lt_s.p90_i;
	data.P90 = agg_stats->lt_s.p90;
	data.P90_k = agg_stats->lt_s.p90_k;
	data.P95_i = agg_stats->lt_s.p95_i;
	data.P95 = agg_stats->lt_s.p95;
	data.P95_k = agg_stats->lt_s.p95_k;
	data.P99_i = agg_stats->lt_s.p99_i;
	data.P99 = agg_stats->lt_s.p99;
	data.P99_k = agg_stats->lt_s.p99_k;

	iov[0].iov_base = &m;
	iov[0].iov_len = sizeof(struct msg1);
	iov[1].iov_base = &data;
	iov[1].iov_len = sizeof(struct latency_reply);
	to_send = sizeof(struct msg_hdr) + sizeof(uint32_t) +
			  sizeof(struct latency_reply);
	iovcnt = 2;

	conv = compute_convergence(agg_stats->lt_s.samples, agg_stats->lt_s.size);

	if (get_agent_type() == SYMMETRIC_NIC_TIMESTAMP_AGENT) {
		//#ifndef SINGLE_REQ
		// conv = compute_convergence(agg_stats->lt_s.samples,
		// agg_stats->lt_s.size);
		pearson_corr = check_iid(&agg_stats->lt_s);
		//#endif

		m1.Hdr.MessageType = REPLY;
		m1.Hdr.MessageLength = sizeof(2 * sizeof(uint32_t));
		m1.Info = REPLY_CONVERGENCE;

		iov[2].iov_base = &m1;
		iov[2].iov_len = sizeof(struct msg1);
		iov[3].iov_base = &conv;
		iov[3].iov_len = sizeof(uint32_t);
		to_send += sizeof(struct msg1) + sizeof(uint32_t);

		m2.Hdr.MessageType = REPLY;
		m2.Hdr.MessageLength = sizeof(2 * sizeof(uint32_t));
		m2.Info = REPLY_IID;

		iov[4].iov_base = &m2;
		iov[4].iov_len = sizeof(struct msg1);
		iov[5].iov_base = &pearson_corr;
		iov[5].iov_len = sizeof(double);
		to_send += sizeof(struct msg1) + sizeof(double);

		m3.Hdr.MessageType = REPLY;
		m3.Hdr.MessageLength = 2 * sizeof(uint32_t);
		m3.Info1 = REPLY_IA_COMP;
		m3.Info2 = check_ia();

		iov[6].iov_base = &m3;
		iov[6].iov_len = sizeof(struct msg2);
		to_send += sizeof(struct msg2);

		iovcnt = 7;
	}
	n = writev(sockfd, iov, iovcnt);
	assert(n == to_send);
}

static void reply_ack(int sockfd)
{
	int n;
	struct iovec reply[2];
	struct msg1 m;

	m.Hdr.MessageType = REPLY;
	m.Hdr.MessageLength = sizeof(uint32_t);
	m.Info = REPLY_ACK;

	reply[0].iov_base = &m;
	reply[0].iov_len = sizeof(struct msg1);

	n = writev(sockfd, reply, 1);
	assert(n == (sizeof(struct msg_hdr) + sizeof(uint32_t)));
}

int manager_run(void)
{
	int sockfd, newsockfd, n;
	struct msg_hdr hdr;
	int payload1;
	double sampling;

	sockfd = create_socket();
	if (sockfd < 0)
		return -1;
	newsockfd = accept_conn(sockfd);
	if (newsockfd < 0)
		return -1;

	while (1) {
		// process incoming messages
		n = read(newsockfd, &hdr, sizeof(struct msg_hdr));
		if (n < 0) {
			lancet_perror("ERROR reading from socket");
			return -1;
		}
		if (n == 0)
			return 0;

		assert(n == (int)sizeof(struct msg_hdr));

		switch (hdr.MessageType) {
		case START_LOAD:
			n = read(newsockfd, &payload1, sizeof(uint32_t));
			assert(n == sizeof(uint32_t));
			set_load(payload1);
			agents_should_measure = 0;
			clear_all_stats();
			agents_should_load = 1;
			reply_ack(newsockfd);
			break;
		case START_MEASURE:
			n = read(newsockfd, &payload1, sizeof(uint32_t));
			assert(n == sizeof(uint32_t));
			n = read(newsockfd, &sampling, sizeof(double));
			assert(n == sizeof(double));
			clear_all_stats();
			set_per_thread_samples(lround(1.01 * payload1 / get_thread_count()),
								   sampling);
			start_measure_time = time_us();
			agents_should_measure = 1;
			reply_ack(newsockfd);
			// prepare reference_ia for the ks test
			collect_reference_ia(get_ia_gen());
			break;
		case REPORT_REQ:
			n = read(newsockfd, &payload1, sizeof(uint32_t));
			assert(n == sizeof(uint32_t));
			if (agents_should_measure) {
				agents_should_measure = 0;
				stop_measure_time = time_us();
			}
			if (payload1 == REPORT_THROUGHPUT)
				reply_throughput_stats(newsockfd);
			else if (payload1 == REPORT_LATENCY)
				reply_latency_stats(newsockfd);
#if 0
				else if (payload1 == REPORT_CONVERGENCE)
					reply_conv_stats(newsockfd);
#endif
			else {
				lancet_fprintf(stderr, "Unknown report  message\n");
				return -1;
			}
			agents_should_measure = 1;
			break;
		default:
			lancet_fprintf(stderr, "Unknown message\n");
			return -1;
		}
	}
	return 0;
}
