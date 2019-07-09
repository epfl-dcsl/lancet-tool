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
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

#include <lancet/agent.h>
#include <lancet/app_proto.h>

int open_connection(const char *host, const char *port)
{
	int fd;
	struct in_addr ip;
	uint16_t nport = strtol(port, NULL, 10);
	struct sockaddr_in addr;

	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) {
		perror("socket");
		return -1;
	}

	if (inet_aton(host, &ip) == 0) {
		perror("inet_aton");
		return -1;
	}

	addr.sin_port = htons(nport);
	addr.sin_addr = ip;
	addr.sin_family = AF_INET;

	if (connect(fd, (struct sockaddr *)&addr, sizeof(addr))) {
		perror("connect");
		return -1;
	}

	return fd;
}

int main(int argc, char **argv)
{
	int sock, len, key_count;
	struct application_protocol *proto;
	struct request req;
	struct iovec received;
	char buf[1024];
	struct byte_req_pair pair;

	if (argc != 4) {
		fprintf(stderr, "usage: %s <proto_desc> <host> <port>\n", argv[0]);
		return EXIT_FAILURE;
	}

	proto = init_app_proto(argv[1]);

	assert(proto != NULL);

	sock = open_connection(argv[2], argv[3]);
	assert(sock > 0);

	key_count = kv_get_key_count(proto);

	for (int i = 0; i < key_count; i++) {
		if (create_request(proto, &req)) {
			fprintf(stderr, "failed to create request %d\n", i);
			return EXIT_FAILURE;
		}

		len = writev(sock, req.iovs, req.iov_cnt);
		assert(len > 0);

		len = read(sock, buf, 1024);
		received.iov_base = buf;
		received.iov_len = len;
		pair = consume_response(proto, &received);
		assert(pair.reqs >= 1);
	}

	return EXIT_SUCCESS;
}
