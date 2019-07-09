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
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <time.h>

#include <lancet/error.h>
#include <lancet/manager.h>
#include <lancet/misc.h>
#include <lancet/timestamping.h>
#include <lancet/tp_proto.h>

static __thread int epoll_fd;
static __thread struct udp_socket *sockets;
static __thread uint32_t socket_idx = 0;

/*
 * Socket management
 */
static inline struct udp_socket *get_socket()
{
	int idx;
	struct udp_socket *c;

	idx = socket_idx++ % (get_conn_count() / get_thread_count());
	c = &sockets[idx];
	if (!c->taken) {
		c->taken = 1;
		return c;
	}

	return NULL;
}

static int create_latency_sockets(void)
{
	struct sockaddr_in addr;
	int i, ret, sock, per_thread_conn, million = 1e6, dest_idx;
	struct host_tuple *targets;
	struct timeval tv;

	addr.sin_family = AF_INET;
	per_thread_conn = get_conn_count() / get_thread_count();
	sockets = calloc(per_thread_conn, sizeof(struct udp_socket));
	assert(sockets);
	targets = get_targets();

	tv.tv_sec = 2;
	tv.tv_usec = 0;

	for (i = 0; i < per_thread_conn; i++) {
		sock = socket(AF_INET, SOCK_DGRAM, 0);
		if (sock == -1) {
			lancet_perror("Error creating socket");
			return -1;
		}

		/* Enable busy polling */
		ret = setsockopt(sock, SOL_SOCKET, SO_BUSY_POLL, &million,
						 sizeof(million));
		if (ret) {
			lancet_perror("Error setsockopt SO_BUSY_POLL");
			return -1;
		}

		ret = setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (struct timeval *)&tv,
						 sizeof(struct timeval));
		if (ret) {
			lancet_perror("Error setsockopt SO_RCVTIMEO");
			return -1;
		}

		dest_idx = i % get_target_count();
		addr.sin_port = htons(targets[dest_idx].port);
		addr.sin_addr.s_addr = targets[dest_idx].ip;
		ret = connect(sock, (struct sockaddr *)&addr, sizeof(addr));
		if (ret) {
			lancet_perror("Error connecting");
			return -1;
		}

		sockets[i].fd = sock;
		sockets[i].taken = 0;
	}

	return 0;
}

static int create_throughput_socket(void)
{
	struct sockaddr_in addr;
	int i, efd, ret, sock, per_thread_conn, dest_idx;
	struct epoll_event event;
	struct host_tuple *targets;
	struct timeval tv;

	addr.sin_family = AF_INET;
	/* Init epoll*/
	efd = epoll_create(1);
	if (efd < 0) {
		lancet_perror("epoll_create error");
		return -1;
	}

	per_thread_conn = get_conn_count() / get_thread_count();
	sockets = calloc(per_thread_conn, sizeof(struct udp_socket));
	assert(sockets);
	targets = get_targets();

	tv.tv_sec = 2;
	tv.tv_usec = 0;

	for (i = 0; i < per_thread_conn; i++) {
		sock = socket(AF_INET, SOCK_DGRAM, 0);
		if (sock == -1) {
			lancet_perror("Error creating socket");
			return -1;
		}

		ret = fcntl(sock, F_SETFL, O_NONBLOCK);
		if (ret == -1) {
			lancet_perror("Error while setting nonblocking");
			return -1;
		}

		ret = setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (struct timeval *)&tv,
						 sizeof(struct timeval));
		if (ret) {
			lancet_perror("Error setsockopt SO_RCVTIMEO");
			return -1;
		}

		if (get_agent_type() == SYMMETRIC_NIC_TIMESTAMP_AGENT) {
			if (setsockopt(sock, SOL_SOCKET, SO_BINDTODEVICE, get_if_name(),
						   strlen(get_if_name()))) {
				lancet_perror("setsockopt SO_BINDTODEVICE");
				return -1;
			}
			ret = sock_enable_timestamping(sock);
			if (ret) {
				lancet_fprintf(stderr, "sock enable timestamping failed\n");
				return -1;
			}
		}

		dest_idx = i % get_target_count();
		addr.sin_port = htons(targets[dest_idx].port);
		addr.sin_addr.s_addr = targets[dest_idx].ip;
		ret = connect(sock, (struct sockaddr *)&addr, sizeof(addr));
		if (ret) {
			lancet_perror("Error connecting");
			return -1;
		}

		sockets[i].fd = sock;
		sockets[i].taken = 0;

		// Add socket to epoll group
		event.events = EPOLLIN;
		event.data.ptr = (void *)&sockets[i].fd;
		ret = epoll_ctl(efd, EPOLL_CTL_ADD, sock, &event);
		if (ret) {
			lancet_perror("Error while adding to epoll group");
			return -1;
		}
	}
	epoll_fd = efd;
	return 0;
}

static void latency_udp_main(void)
{
	int i, ret, bytes_to_send;
	long start_time, end_time, next_tx;
	struct udp_socket *socket;
	struct request *to_send;
	struct byte_req_pair read_res;
	struct byte_req_pair send_res;

	if (create_latency_sockets())
		return;

	next_tx = time_ns();
	while (1) {
		if (!should_load()) {
			next_tx = time_ns();
			continue;
		}
		if (time_ns() < next_tx)
			continue;
		socket = get_socket();
		if (!socket)
			continue;

		to_send = prepare_request();
		bytes_to_send = 0;
		for (i = 0; i < to_send->iov_cnt; i++)
			bytes_to_send += to_send->iovs[i].iov_len;
		assert(bytes_to_send <= UDP_MAX_PAYLOAD);
		start_time = time_ns();
		ret = writev(socket->fd, to_send->iovs, to_send->iov_cnt);
		if (ret < 0) {
			lancet_perror("Writev failed\n");
			return;
		}
		assert(ret == bytes_to_send);

		send_res.bytes = ret;
		send_res.reqs = 1;

		/* Bookkeeping */
		add_throughput_tx_sample(send_res);

		ret = recv(socket->fd, socket->buffer, UDP_MAX_PAYLOAD, 0);
		if (ret < 0) {
			lancet_perror("Error read\n");
			return;
		}
		read_res = process_response(socket->buffer, ret);
		assert(read_res.bytes == ret);
		end_time = time_ns();

		/*BookKeeping*/
		add_throughput_rx_sample(read_res);
		add_latency_sample((end_time - start_time), NULL);

		/* Mark socket as available */
		socket->taken = 0;

		/* Schedule next */
		next_tx += get_ia();
	}
}

static void throughput_udp_main(void)
{
	int ready, i, conn_per_thread, ret, bytes_to_send;
	long next_tx;
	struct epoll_event *events;
	struct udp_socket *socket;
	struct request *to_send;
	struct byte_req_pair read_res;
	struct byte_req_pair send_res;
	struct timespec tx_timestamp;

	if (create_throughput_socket())
		return;

	/*Initializations*/
	conn_per_thread = get_conn_count() / get_thread_count();
	events = malloc(conn_per_thread * sizeof(struct epoll_event));

	next_tx = time_ns();
	while (1) {
		if (!should_load()) {
			next_tx = time_ns();
			continue;
		}
		while (time_ns() >= next_tx) {
			socket = get_socket();
			if (!socket)
				goto REP_PROC;
			to_send = prepare_request();
			bytes_to_send = 0;
			for (i = 0; i < to_send->iov_cnt; i++)
				bytes_to_send += to_send->iovs[i].iov_len;
			assert(bytes_to_send <= UDP_MAX_PAYLOAD);

			time_ns_to_ts(&tx_timestamp);
			add_tx_timestamp(&tx_timestamp);

			ret = writev(socket->fd, to_send->iovs, to_send->iov_cnt);
			if ((ret < 0) && (errno != EWOULDBLOCK)) {
				lancet_perror("Unknown connection error write\n");
				return;
			}
			assert(ret == bytes_to_send);

			/*BookKeeping*/
			send_res.bytes = ret;
			send_res.reqs = 1;
			add_throughput_tx_sample(send_res);

			/*Schedule next*/
			next_tx += get_ia();
		}
	REP_PROC:
		/* process responses */
		ready = epoll_wait(epoll_fd, events, conn_per_thread, 0);
		for (i = 0; i < ready; i++) {
			socket = (struct udp_socket *)events[i].data.ptr;
			/* Handle incoming packet */
			if (events[i].events & EPOLLIN) {
				ret = recv(socket->fd, socket->buffer, UDP_MAX_PAYLOAD, 0);
				if ((ret < 0) && (errno != EWOULDBLOCK)) {
					lancet_perror("Unknow connection error read\n");
					return;
				}
				read_res = process_response(socket->buffer, ret);
				assert(read_res.bytes == ret);
				/* Bookkeeping */
				add_throughput_rx_sample(read_res);

				// Mark socket as available
				socket->taken = 0;
			} else if (events[i].events & EPOLLHUP)
				assert(0);
			else
				assert(0);
		}
	}
}

static void symmetric_nic_udp_main(void)
{
	int ready, i, conn_per_thread, ret, bytes_to_send;
	long next_tx;
	struct epoll_event *events;
	struct udp_socket *socket;
	struct request *to_send;
	struct byte_req_pair read_res;
	struct byte_req_pair send_res;
	struct msghdr hdr;
	struct timespec latency, tx_timestamp;
	struct timestamp_info rx_timestamp;

	if (create_throughput_socket())
		return;

	/*Initializations*/
	conn_per_thread = get_conn_count() / get_thread_count();
	events = malloc(conn_per_thread * sizeof(struct epoll_event));

	next_tx = time_ns();
	while (1) {
		if (!should_load()) {
			next_tx = time_ns();
			continue;
		}
		if (time_ns() >= next_tx) {
			socket = get_socket();
			if (!socket)
				goto REP_PROC;
			to_send = prepare_request();

			bytes_to_send = 0;
			for (i = 0; i < to_send->iov_cnt; i++)
				bytes_to_send += to_send->iovs[i].iov_len;

			assert(bytes_to_send <= UDP_MAX_PAYLOAD);
			bzero(&hdr, sizeof(hdr));
			hdr.msg_iov = to_send->iovs;
			hdr.msg_iovlen = to_send->iov_cnt;
			ret = sendmsg(socket->fd, &hdr, 0);
			if ((ret < 0) && (errno != EWOULDBLOCK)) {
				lancet_perror("Unknown connection error write\n");
				return;
			}
			assert(ret == bytes_to_send);

			send_res.bytes = ret;
			send_res.reqs = 1;

			/*BookKeeping*/
			add_throughput_tx_sample(send_res);

			/*Schedule next*/
			next_tx += get_ia();
		}
	REP_PROC:
		/* process responses */
		ready = epoll_wait(epoll_fd, events, conn_per_thread, 0);
		for (i = 0; i < ready; i++) {
			socket = (struct udp_socket *)events[i].data.ptr;
			/* Handle incoming packet */
			if (events[i].events & EPOLLIN) {
				ret = timestamp_recv(socket->fd, socket->buffer,
									 UDP_MAX_PAYLOAD, 0, &rx_timestamp);
				if ((ret < 0) && (errno != EWOULDBLOCK)) {
					lancet_perror("Unknow connection error read\n");
					return;
				}

				/* Copy rx_timestamp into socket->rx_timestamp */
				assert(rx_timestamp.time.tv_sec != 0);
				socket->rx_timestamp.tv_sec = rx_timestamp.time.tv_sec;
				socket->rx_timestamp.tv_nsec = rx_timestamp.time.tv_nsec;

				read_res = process_response(socket->buffer, ret);
				assert(read_res.bytes == ret);

				// Retrieve tx timestamp in case it was out of order
				if (socket->tx_timestamp.tv_sec == 0) {
					udp_get_tx_timestamp(socket->fd, &socket->tx_timestamp);
				}

				ret = timespec_diff(&latency, &socket->rx_timestamp,
									&socket->tx_timestamp);
				if (ret == 0) {
					add_latency_sample(latency.tv_nsec + latency.tv_sec * 1e9,
									   &socket->tx_timestamp);
				}

				/* Bookkeeping */
				add_throughput_rx_sample(read_res);

				/* Mark socket as available */
				socket->taken = 0;

				/* Reset timestamps to make sure the next iteration on this
				 * socket doesn't use old values */
				socket->tx_timestamp.tv_sec = 0;
				socket->rx_timestamp.tv_sec = 0;
				rx_timestamp.time.tv_sec = 0;
			} else if (events[i].events & EPOLLERR) {
				/* If an outgoing packet is fragmented, then only the first
				 * fragment is timestamped and returned to the
				 * sending socket. Thus we can directly store the timestamp in
				 * socket->tx_timestamp. */
				ret = udp_get_tx_timestamp(socket->fd, &tx_timestamp);
				if (ret == 1 && socket->taken) {
					socket->tx_timestamp.tv_sec = tx_timestamp.tv_sec;
					socket->tx_timestamp.tv_nsec = tx_timestamp.tv_nsec;
					add_tx_timestamp(&socket->tx_timestamp);
				}
			} else if (events[i].events & EPOLLHUP)
				assert(0);
			else
				assert(0);

			if ((time_ns() - next_tx) > 0)
				break;
		}
	}
}

static void symmetric_udp_main(void)
{
	int ready, i, conn_per_thread, ret, bytes_to_send;
	long next_tx;
	struct epoll_event *events;
	struct udp_socket *socket;
	struct request *to_send;
	struct byte_req_pair read_res;
	struct byte_req_pair send_res;
	struct msghdr hdr;
	struct timespec latency;

	if (create_throughput_socket())
		return;

	/*Initializations*/
	conn_per_thread = get_conn_count() / get_thread_count();
	events = malloc(conn_per_thread * sizeof(struct epoll_event));

	next_tx = time_ns();
	while (1) {
		if (!should_load()) {
			next_tx = time_ns();
			continue;
		}
		if (time_ns() >= next_tx) {
			socket = get_socket();
			if (!socket)
				goto REP_PROC;
			to_send = prepare_request();
			bytes_to_send = 0;
			for (i = 0; i < to_send->iov_cnt; i++)
				bytes_to_send += to_send->iovs[i].iov_len;

			assert(bytes_to_send <= UDP_MAX_PAYLOAD);
			bzero(&hdr, sizeof(hdr));
			hdr.msg_iov = to_send->iovs;
			hdr.msg_iovlen = to_send->iov_cnt;

			time_ns_to_ts(&socket->tx_timestamp);
			ret = sendmsg(socket->fd, &hdr, 0);
			if ((ret < 0) && (errno != EWOULDBLOCK)) {
				lancet_perror("Unknown connection error write\n");
				return;
			}
			assert(ret == bytes_to_send);

			send_res.bytes = ret;
			send_res.reqs = 1;

			/*BookKeeping*/
			add_throughput_tx_sample(send_res);
			add_tx_timestamp(&socket->tx_timestamp);

			/*Schedule next*/
			next_tx += get_ia();
		}
	REP_PROC:
		/* process responses */
		ready = epoll_wait(epoll_fd, events, conn_per_thread, 0);
		for (i = 0; i < ready; i++) {
			socket = (struct udp_socket *)events[i].data.ptr;
			/* Handle incoming packet */
			if (events[i].events & EPOLLIN) {
				ret = recv(socket->fd, socket->buffer, UDP_MAX_PAYLOAD, 0);
				if ((ret < 0) && (errno != EWOULDBLOCK)) {
					lancet_perror("Unknow connection error read\n");
					return;
				}

				time_ns_to_ts(&socket->rx_timestamp);

				read_res = process_response(socket->buffer, ret);
				assert(read_res.bytes == ret);

				ret = timespec_diff(&latency, &socket->rx_timestamp,
									&socket->tx_timestamp);
				if (ret == 0) {
					add_latency_sample(latency.tv_nsec + latency.tv_sec * 1e9,
									   &socket->tx_timestamp);
				}

				/* Bookkeeping */
				add_throughput_rx_sample(read_res);

				/* Mark socket as available */
				socket->taken = 0;
			} else if (events[i].events & EPOLLHUP)
				assert(0);
			else
				assert(0);
		}
	}
}

struct transport_protocol *init_udp(void)
{
	struct transport_protocol *tp;

	tp = malloc(sizeof(struct transport_protocol));
	if (!tp) {
		lancet_fprintf(stderr, "Failed to alloc transport_protocol\n");
		return NULL;
	}

	tp->tp_main[THROUGHPUT_AGENT] = throughput_udp_main;
	tp->tp_main[LATENCY_AGENT] = latency_udp_main;
	tp->tp_main[SYMMETRIC_NIC_TIMESTAMP_AGENT] = symmetric_nic_udp_main;
	tp->tp_main[SYMMETRIC_AGENT] = symmetric_udp_main;

	return tp;
}
