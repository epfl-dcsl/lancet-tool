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
/*
 * Interface for the communication protocol e.g. TCP, UDP, R2P2 etc
 */
#pragma once

#include <lancet/agent.h>
#include <lancet/stats.h>
#include <stdlib.h>
#include <time.h>

struct transport_protocol {
	void (*tp_main[AGENT_NR])(void);
};

struct transport_protocol *init_tcp(void);
#ifdef ENABLE_R2P2
struct transport_protocol *init_r2p2(void);
#endif
struct transport_protocol *init_udp(void);

/*
 * TCP specific
 */
#define MAX_PAYLOAD 16384
struct tcp_connection {
	uint32_t fd;
	uint16_t idx;
	uint16_t closed;
	uint16_t pending_reqs;
	uint16_t buffer_idx;
	char buffer[MAX_PAYLOAD];
};

/*
 * UDP specific
 */
#define UDP_MAX_PAYLOAD 1500
struct udp_socket {
	uint32_t fd;
	uint32_t taken;
	struct timespec tx_timestamp;
	struct timespec rx_timestamp;
	char buffer[UDP_MAX_PAYLOAD];
};
