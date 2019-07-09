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
#include <arpa/inet.h>
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <lancet/app_proto.h>
#include <lancet/key_gen.h>
#include <lancet/memcache_bin.h>
#include <lancet/rand_gen.h>

static __thread struct bmc_header header;
static __thread uint64_t extras;
static __thread char val_len_str[64];
static char get_cmd[] = "get ";
static char set_cmd[] = "set ";
static char rn[] = "\r\n";
static char set_zeros[] = " 0 0 ";

enum { WAIT_FOR_HEADER = 0, WAIT_FOR_BODY, FINISHED };

static char *strchnth(char *s, char c, int occ)
{
	char *ptr;

	ptr = s - 1;
	for (int i = 0; i < occ; i++) {
		ptr = strchr(ptr + 1, c);
		if (!ptr)
			return NULL;
	}
	return ptr;
}

static struct byte_req_pair
memcache_ascii_consume_response(struct application_protocol *proto,
								struct iovec *resp)
{
	struct byte_req_pair res;
	int bytes_to_process, get_rep_size;
	char *buf, *ptr;

	res.bytes = 0;
	res.reqs = 0;
	bytes_to_process = resp->iov_len;
	buf = resp->iov_base;

	while (bytes_to_process) {
		if (bytes_to_process < 5) // minimum reply is EDN\r\n
			goto OUT;
		if (strncmp(&buf[resp->iov_len - bytes_to_process], "END\r\n", 5) ==
			0) {
			// key not found
			res.bytes += 5;
			res.reqs += 1;
			bytes_to_process -= 5;
			continue;
		}
		if (bytes_to_process < 8) // try STORED\r\n
			goto OUT;
		if (strncmp(&buf[resp->iov_len - bytes_to_process], "STORED\r\n", 8) ==
			0) {
			// successful set
			res.bytes += 8;
			res.reqs += 1;
			bytes_to_process -= 8;
			continue;
		}
		// try for get reply - look for 3 \n
		ptr = strchnth(&buf[resp->iov_len - bytes_to_process], '\n', 3);
		if (!ptr)
			goto OUT;
		get_rep_size = ptr - &buf[resp->iov_len - bytes_to_process] + 1;
		bytes_to_process -= get_rep_size;
		res.bytes += get_rep_size;
		res.reqs += 1;
	}

OUT:
	return res;
}

static int memcache_ascii_create_request(struct application_protocol *proto,
										 struct request *req)
{
	struct kv_info *info;
	long val_len;
	int key_idx;
	struct iovec *key;

	info = (struct kv_info *)proto->arg;
	key_idx = generate(info->key_sel);
	key = &info->key->keys[key_idx];

	if (drand48() > info->get_ratio) {
		// set
		val_len = lround(generate(info->val_len));
		assert(val_len <= MAX_VAL_SIZE);
		snprintf(val_len_str, 64, "%ld", val_len);

		req->iovs[0].iov_base = set_cmd;
		req->iovs[0].iov_len = 4;
		req->iovs[1].iov_base = key->iov_base;
		req->iovs[1].iov_len = key->iov_len;
		req->iovs[2].iov_base = set_zeros;
		req->iovs[2].iov_len = 5;
		req->iovs[3].iov_base = val_len_str;
		req->iovs[3].iov_len = strlen(val_len_str);
		req->iovs[4].iov_base = rn;
		req->iovs[4].iov_len = 2;
		req->iovs[5].iov_base = random_char;
		req->iovs[5].iov_len = val_len;
		req->iovs[6].iov_base = rn;
		req->iovs[6].iov_len = 2;

		req->iov_cnt = 7;
	} else {
		// get
		req->iovs[0].iov_base = get_cmd;
		req->iovs[0].iov_len = 4;
		req->iovs[1].iov_base = key->iov_base;
		req->iovs[1].iov_len = key->iov_len;
		req->iovs[2].iov_base = rn;
		req->iovs[2].iov_len = 2;

		req->iov_cnt = 3;
	}

	return 0;
}

static struct byte_req_pair
memcache_bin_consume_response(struct application_protocol *proto,
							  struct iovec *resp)
{
	struct byte_req_pair res;
	int bytes_to_process, state;
	struct bmc_header *bmc_header;
	char *buf;

	res.bytes = 0;
	res.reqs = 0;

	bytes_to_process = resp->iov_len;

	state = WAIT_FOR_HEADER;
	buf = resp->iov_base;
	while (bytes_to_process) {
		switch (state) {
		case WAIT_FOR_HEADER:
			if (bytes_to_process < sizeof(struct bmc_header))
				goto OUT;
			bmc_header =
				(struct bmc_header *)&buf[resp->iov_len - bytes_to_process];
			bmc_header->body_len = ntohl(bmc_header->body_len);
			state = WAIT_FOR_BODY;
			break;
		case WAIT_FOR_BODY:
			if (bytes_to_process <
				(bmc_header->body_len + sizeof(struct bmc_header)))
				goto OUT;
			state = FINISHED;
			break;
		case FINISHED:
			bytes_to_process -=
				(bmc_header->body_len + sizeof(struct bmc_header));
			res.reqs += 1;
			res.bytes += (sizeof(struct bmc_header) + bmc_header->body_len);
			state = WAIT_FOR_HEADER;
			break;
		}
	}
OUT:
	return res;
}

static int memcache_bin_create_request(struct application_protocol *proto,
									   struct request *req)
{
	struct kv_info *info;
	long val_len;
	int key_idx;
	struct iovec *key;

	bzero(&header, sizeof(struct bmc_header));
	extras = 0;

	info = (struct kv_info *)proto->arg;
	key_idx = generate(info->key_sel);
	key = &info->key->keys[key_idx];

	header.magic = 0x80;
	header.key_len = htons(key->iov_len);
	header.data_type = 0x00;
	header.vbucket = 0x00;
	assert(key != NULL);

	if (drand48() > info->get_ratio) {
		// set
		val_len = lround(generate(info->val_len));
		assert(val_len <= MAX_VAL_SIZE);

		header.opcode = CMD_SET;
		header.extra_len = 0x08; // sets have extras for flags and expiration
		header.body_len = htonl(key->iov_len + val_len + header.extra_len);

		req->iovs[0].iov_base = &header;
		req->iovs[0].iov_len = sizeof(struct bmc_header);
		req->iovs[1].iov_base = &extras;
		req->iovs[1].iov_len = sizeof(uint64_t);
		req->iovs[2].iov_base = key->iov_base;
		req->iovs[2].iov_len = key->iov_len;
		req->iovs[3].iov_base = random_char;
		req->iovs[3].iov_len = val_len;

		req->iov_cnt = 4;
	} else {
		// get
		header.opcode = CMD_GET;
		header.extra_len = 0x00;
		header.body_len = htonl(key->iov_len);

		req->iovs[0].iov_base = &header;
		req->iovs[0].iov_len = sizeof(struct bmc_header);
		req->iovs[1].iov_base = key->iov_base;
		req->iovs[1].iov_len = key->iov_len;

		req->iov_cnt = 2;
	}

	return 0;
}

int memcache_init(char *proto, struct application_protocol *app_proto)
{
	struct kv_info *data;
	char *token, *key_dist;
	int key_count;
	char *saveptr;
	char key_sel[64];

	data = malloc(sizeof(struct kv_info));
	assert(data != NULL);

	assert(strncmp("memcache-", proto, 9) == 0);

	/* key size dist */
	strtok_r(proto, "_", &saveptr);

	key_dist = strtok_r(NULL, "_", &saveptr);

	token = strtok_r(NULL, "_", &saveptr);

	/* value length dist */
	data->val_len = init_rand(token);

	assert(data->val_len != NULL);

	/* key count */
	token = strtok_r(NULL, "_", &saveptr);

	key_count = atoi(token);

	/* init key generator */
	data->key = init_key_gen(key_dist, key_count);
	assert(data->key != NULL);

	/* get request ratio */
	token = strtok_r(NULL, "_", &saveptr);
	data->get_ratio = strtod(token, NULL);

	/* key selector distribution */
	token = strtok_r(NULL, "_", &saveptr);
	sprintf(key_sel, "%s:%d\n", token, key_count);
	data->key_sel = init_rand(key_sel);
	assert(data->key_sel != NULL);

	app_proto->arg = data;
	if (strncmp("memcache-bin", proto, 12) == 0) {
		app_proto->type = PROTO_MEMCACHED_BIN;
		app_proto->consume_response = memcache_bin_consume_response;
		app_proto->create_request = memcache_bin_create_request;
	} else if (strncmp("memcache-ascii", proto, 14) == 0) {
		app_proto->type = PROTO_MEMCACHED_ASCII;
		app_proto->consume_response = memcache_ascii_consume_response;
		app_proto->create_request = memcache_ascii_create_request;
	} else {
		fprintf(stderr, "Wrong memcached protocol\n");
		return -1;
	}

	return 0;
}
