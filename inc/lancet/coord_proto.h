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

#include <stdint.h>

/*
 * Message types
 */
enum {
	START_LOAD = 0,
	START_MEASURE,
	REPORT_REQ,
	REPLY,
	TERMINATE,
};

/*
 * Types of REPORT_REQ messages
 */
enum {
	REPORT_THROUGHPUT = 0,
	REPORT_LATENCY,
};

/*
 * Types of REPLY messages
 */
enum {
	REPLY_ACK = 0,
	REPLY_STATS_THROUGHPUT,
	REPLY_STATS_LATENCY,
	REPLY_CONVERGENCE,
	REPLY_IA_COMP,
	REPLY_IID,
	// REPLY_KV_STATS etc...
};

struct __attribute__((__packed__)) msg_hdr {
	uint32_t MessageType;
	uint32_t MessageLength;
};

struct __attribute__((__packed__)) msg1 {
	struct msg_hdr Hdr;
	uint32_t Info;
};

struct __attribute__((__packed__)) msg2 {
	struct msg_hdr Hdr;
	uint32_t Info1;
	uint32_t Info2;
};

// the reply payload starts with the reply type which is a uint32_t
struct __attribute__((__packed__)) throughput_reply {
	uint64_t Rx_bytes;
	uint64_t Tx_bytes;
	uint64_t Req_count;
	uint64_t Duration;
	uint64_t CorrectIAD; // to avoid padding
};

struct __attribute__((__packed__)) latency_reply {
	struct throughput_reply Th_data;
	uint64_t Avg_lat;
	uint64_t P50_i;
	uint64_t P50;
	uint64_t P50_k;
	uint64_t P90_i;
	uint64_t P90;
	uint64_t P90_k;
	uint64_t P95_i;
	uint64_t P95;
	uint64_t P95_k;
	uint64_t P99_i;
	uint64_t P99;
	uint64_t P99_k;
	uint32_t ToReduceSampling;
	uint8_t IsIid;
	uint8_t IsStationary;
};
