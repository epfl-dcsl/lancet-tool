#include <stdio.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <time.h>

#include <lancet/error.h>
#include <lancet/misc.h>
#include <lancet/timestamping.h>
#include <lancet/tp_proto.h>

#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/ssl.h>

static SSL_CTX *ssl_ctx;
static __thread struct tls_connection *connections;
static __thread int epoll_fd;
static __thread struct pending_tx_timestamps *per_conn_tx_timestamps;
static __thread uint32_t conn_idx = 0;

static inline struct tls_connection *pick_conn()
{
	int idx;
	struct tls_connection *c;

	// FIXME: Consider picking connection round robin
	// idx = rand() % (get_conn_count() / get_thread_count()) ;
	idx = conn_idx++ % (get_conn_count() / get_thread_count());
	c = &connections[idx];
	if ((c->conn.pending_reqs < get_max_pending_reqs()) && (!c->conn.closed))
		return c;

	return NULL;
}

static int ssl_init(void)
{
	/* Load encryption & hashing algorithms for the SSL program */
	SSL_library_init();

	/* Load the error strings for SSL & CRYPTO APIs */
	SSL_load_error_strings();

	// SSL context for the process. All connections will share one
	// process level context.
	ssl_ctx = SSL_CTX_new(TLS_client_method());
	if (!ssl_ctx)
		return -1;

	SSL_CTX_set_min_proto_version(ssl_ctx, TLS1_2_VERSION);

	/* Don't verify the certificate */
	SSL_CTX_set_verify(ssl_ctx, SSL_VERIFY_NONE, NULL);

	/* Don't use session caching */
	SSL_CTX_set_session_cache_mode(ssl_ctx, SSL_SESS_CACHE_OFF);

	return 0;
}

static int ssl_init_connection(struct tls_connection *tls_conn)
{
	tls_conn->ssl = SSL_new(ssl_ctx);
	assert(tls_conn->ssl);

	int err = SSL_set_fd(tls_conn->ssl, tls_conn->conn.fd);
	assert(err == 1);

	/*
	 * Assume that connection is in blocking mode
	 */
	err = SSL_connect(tls_conn->ssl);
	if (err <= 0) {
		lancet_fprintf(stderr, "Failed to ssl connect\n");
		return -1;
	}

	err = SSL_is_init_finished(tls_conn->ssl);
	assert(err == 1);

	return 0;
}

static int throughput_open_connections(void)
{
	/*init epoll*/
	struct sockaddr_in addr;
	int i, efd, ret, sock, per_thread_conn, dest_idx, n;
	int one = 1;
	struct epoll_event event;
	struct linger linger;
	struct host_tuple *targets;

	addr.sin_family = AF_INET;
	efd = epoll_create(1);
	if (efd < 0) {
		lancet_perror("epoll_create error");
		return -1;
	}

	per_thread_conn = get_conn_count() / get_thread_count();
	connections = calloc(per_thread_conn, sizeof(struct tls_connection));
	assert(connections);
	if ((get_agent_type() == SYMMETRIC_NIC_TIMESTAMP_AGENT) ||
		(get_agent_type() == SYMMETRIC_AGENT)) {
		per_conn_tx_timestamps =
			calloc(per_thread_conn, sizeof(struct pending_tx_timestamps));
		assert(per_conn_tx_timestamps);
		for (i = 0; i < per_thread_conn; i++) {
			per_conn_tx_timestamps[i].pending =
				calloc(get_max_pending_reqs(), sizeof(struct timestamp_info));
			assert(per_conn_tx_timestamps[i].pending);
		}
	}
	targets = get_targets();

	for (i = 0; i < per_thread_conn; i++) {
		sock = socket(AF_INET, SOCK_STREAM, 0);
		if (sock == -1) {
			lancet_perror("Error creating socket");
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

		connections[i].conn.fd = sock;
		connections[i].conn.pending_reqs = 0;
		connections[i].conn.idx = i;
		connections[i].conn.buffer_idx = 0;
		connections[i].conn.closed = 0;

		/* Init connection in blocking mode */
		if (ssl_init_connection(&connections[i]))
			return -1;

		ret = fcntl(sock, F_SETFL, O_NONBLOCK);
		if (ret == -1) {
			lancet_perror("Error while setting nonblocking");
			return -1;
		}

		n = 524288;
		ret = setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &n, sizeof(n));
		if (ret) {
			lancet_perror("Error setsockopt");
			return -1;
		}
		n = 524288;
		ret = setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &n, sizeof(n));
		if (ret) {
			lancet_perror("Error setsockopt");
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

		/* Disable Nagle's algorithm */
		ret = setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
		if (ret) {
			lancet_perror("Error setsockopt");
			return -1;
		}

		/* Close with RST not FIN */
		linger.l_onoff = 1;
		linger.l_linger = 0;
		if (setsockopt(sock, SOL_SOCKET, SO_LINGER, (void *)&linger,
					   sizeof(linger))) {
			perror("setsockopt(SO_LINGER)");
			exit(1);
		}

		event.events = EPOLLIN;
		event.data.u32 = i;
		ret = epoll_ctl(efd, EPOLL_CTL_ADD, sock, &event);
		if (ret) {
			lancet_perror("Error while adding to epoll group");
			return -1;
		}
	}
	epoll_fd = efd;
	return 0;
}

static void throughput_ssl_main(void)
{
	lancet_fprintf(stderr, "throughput_ssl_main not implemented\n");
	assert(0);
}

static void latency_ssl_main(void)
{
	lancet_fprintf(stderr, "latency_ssl_main not implemented\n");
	assert(0);
}

static void symmetric_nic_ssl_main(void)
{
	lancet_fprintf(stderr, "symmetric_nic_ssl_main not implemented\n");
	assert(0);
}

static void symmetric_ssl_main(void)
{
	int ready, idx, i, j, conn_per_thread, ret, bytes_to_send;
	long next_tx, copied;
	struct epoll_event *events;
	struct tls_connection *conn;
	struct request *to_send;
	struct byte_req_pair read_res;
	struct byte_req_pair send_res;
	struct timespec tx_timestamp, rx_timestamp, latency;
	struct timestamp_info *pending_tx;
	char *wbuf;
	uint64_t wbuf_size = 512;

	wbuf = malloc(wbuf_size);

	if (throughput_open_connections())
		return;

	/*Initializations*/
	conn_per_thread = get_conn_count() / get_thread_count();
	events = malloc(conn_per_thread * sizeof(struct epoll_event));

	pthread_barrier_wait(&conn_open_barrier);
	set_conn_open(1);

	next_tx = time_ns();
	while (1) {
		if (!should_load()) {
			next_tx = time_ns();
			continue;
		}
		while (time_ns() >= next_tx) {
			conn = pick_conn();
			if (!conn)
				goto REP_PROC;
			to_send = prepare_request();

			bytes_to_send = 0;
			for (i = 0; i < to_send->iov_cnt; i++)
				bytes_to_send += to_send->iovs[i].iov_len;

			if (bytes_to_send > wbuf_size) {
				free(wbuf);
				wbuf = malloc(bytes_to_send);
				wbuf_size = bytes_to_send;
			}

			copied = 0;
			for (i = 0; i < to_send->iov_cnt; i++) {
				memcpy(&wbuf[copied], to_send->iovs[i].iov_base,
					   to_send->iovs[i].iov_len);
				copied += to_send->iovs[i].iov_len;
			}
			assert(copied == bytes_to_send);

			int ret = SSL_write(conn->ssl, wbuf, bytes_to_send);

			assert(ret == bytes_to_send);

			time_ns_to_ts(&tx_timestamp);
			push_complete_tx_timestamp(&per_conn_tx_timestamps[conn->conn.idx],
									   &tx_timestamp);
			conn->conn.pending_reqs++;

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
			idx = events[i].data.u32;
			conn = &connections[idx];
			/* Handle incoming packet */
			assert(events[i].events & EPOLLIN);

			// read into the connection buffer
			ret = SSL_read(conn->ssl, &conn->conn.buffer[conn->conn.buffer_idx],
						   MAX_PAYLOAD - conn->conn.buffer_idx);
			if (ret <= 0) {
				int ssl_err = SSL_get_error(conn->ssl, ret);
				if (ssl_err == SSL_ERROR_WANT_READ)
					continue;

				if (ret == 0) {
					SSL_shutdown(conn->ssl);
					SSL_free(conn->ssl);
					close(conn->conn.fd);
					conn->conn.closed = 1;
					continue;
				}

				lancet_fprintf(stderr, "Unexpected SSL error %d\n", ssl_err);
				return;
			}

			time_ns_to_ts(&rx_timestamp);
			conn->conn.buffer_idx += ret;

			read_res = handle_response(&conn->conn);
			if (read_res.reqs == 0)
				continue;

			conn->conn.pending_reqs -= read_res.reqs;
			/*
			 * Assume only the last request will have an rx timestamp!
			 */
			for (j = 0; j < read_res.reqs; j++) {
				pending_tx = pop_pending_tx_timestamps(
					&per_conn_tx_timestamps[conn->conn.idx]);
				assert(pending_tx);
			}
			ret = timespec_diff(&latency, &rx_timestamp, &pending_tx->time);
			assert(ret == 0);
			long diff = latency.tv_nsec + latency.tv_sec * 1e9;
			add_latency_sample(diff, &pending_tx->time);

			/* Bookkeeping */
			add_throughput_rx_sample(read_res);
		}
	}
}

struct transport_protocol *init_tls(void)
{
	struct transport_protocol *tp;

	tp = malloc(sizeof(struct transport_protocol));
	if (!tp) {
		lancet_fprintf(stderr, "Failed to alloc transport_protocol\n");
		return NULL;
	}

	tp->tp_main[THROUGHPUT_AGENT] = throughput_ssl_main;
	tp->tp_main[LATENCY_AGENT] = latency_ssl_main;
	tp->tp_main[SYMMETRIC_NIC_TIMESTAMP_AGENT] = symmetric_nic_ssl_main;
	tp->tp_main[SYMMETRIC_AGENT] = symmetric_ssl_main;

	if (ssl_init()) {
		lancet_fprintf(stderr, "Failed to initate TLS\n");
		return NULL;
	}

	return tp;
}
