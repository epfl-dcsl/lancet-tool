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

#include <lancet/tp_proto.h>

struct timestamp_info {
	struct timespec time;
	uint32_t optid;
};

/*
 * There is one such structure per connection
 */
struct pending_tx_timestamps {
	uint32_t tx_byte_counter; // increment this on every write/send by the
							  // number of bytes (TCP) */
	uint32_t head;			  // waiting for timestamps
	uint32_t tail;			  // timestamp received
	uint32_t consumed;		  // matched with reply
	struct timestamp_info *pending;
};

int enable_nic_timestamping(char *if_name);
int disable_nic_timestamping(char *if_name);
int sock_enable_timestamping(int fd);
ssize_t timestamp_recv(int sockfd, void *buf, size_t len, int flags,
					   struct timestamp_info *last_rx_time);
int get_tx_timestamp(int sockfd, struct pending_tx_timestamps *tx_timestamps);
int udp_get_tx_timestamp(int sockfd, struct timespec *tx_timestamp);
void add_pending_tx_timestamp(struct pending_tx_timestamps *tx_timestamps,
							  uint32_t bytes);
struct timestamp_info *
pop_pending_tx_timestamps(struct pending_tx_timestamps *tx_timestamps);
int timespec_diff(struct timespec *res, struct timespec *a, struct timespec *b);
/*
 * Used only in userspace symmetric timestamping
 */
void push_complete_tx_timestamp(struct pending_tx_timestamps *tx_timestamps,
								struct timespec *to_add);
