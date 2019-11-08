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
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <lancet/app_proto.h>
#include <lancet/key_gen.h>
#include <lancet/rand_gen.h>

static char set_prem[] = "*3\r\n$3\r\nSET\r\n$";
static char get_prem[] = "*2\r\n$3\r\nGET\r\n$";
static char ln[] = "\r\n";
static char dollar[] = "$";
static __thread char key_len_str[64];
static __thread char val_len_str[64];

int static parse_string(char *buf, int bytes_left)
{
	int processed = 0;
	char *p;

	if (bytes_left < 1)
		return processed;

	assert(buf[0] == '+');

	p = memchr(buf, '\n', bytes_left);
	if (!p)
		return processed;

	processed = p - buf + 1;
	return processed;
}

int static parse_bulk_string(char *buf, int bytes_left)
{
	int len, processed = 0, extra;
	char *p;

	if (bytes_left < 1)
		return processed;

	assert(buf[0] == '$');

	p = memchr(buf, '\n', bytes_left);
	if (!p)
		return processed;

	len = atoi(&buf[1]);
	if (len == -1)
		processed = 5; // key not found
	else {
		extra = p - buf + 1 + 2;
		if ((len + extra) <= bytes_left)
			processed = len + extra;
	}

	return processed;
}

struct byte_req_pair redis_consume_response(struct application_protocol *proto,
											struct iovec *resp)
{
	struct byte_req_pair res;
	int bytes_to_process, processed;
	char *buf;

	bytes_to_process = resp->iov_len;
	res.bytes = 0;
	res.reqs = 0;
	buf = resp->iov_base;

	while (bytes_to_process) {
		if (buf[resp->iov_len - bytes_to_process] == '+') {
			processed = parse_string(&buf[resp->iov_len - bytes_to_process],
									 bytes_to_process);
			if (processed == 0)
				break;
			res.bytes += processed;
			res.reqs += 1;
			bytes_to_process -= processed;
		} else if (buf[resp->iov_len - bytes_to_process] == '$') {
			processed = parse_bulk_string(
				&buf[resp->iov_len - bytes_to_process], bytes_to_process);
			if (processed == 0)
				break;
			res.bytes += processed;
			res.reqs += 1;
			bytes_to_process -= processed;
		} else
			assert(0);
	}

	return res;
}

int redis_create_request(struct application_protocol *proto,
						 struct request *req)
{
	struct kv_info *info;
	long val_len;
	int key_idx;
	struct iovec *key;

	info = (struct kv_info *)proto->arg;
	key_idx = generate(info->key_sel);
	key = &info->key->keys[key_idx];

	assert(key != NULL);

	// Fix key
	sprintf(key_len_str, "%ld", key->iov_len);
	req->iovs[1].iov_base = key_len_str;
	req->iovs[1].iov_len = strlen(key_len_str);
	req->iovs[2].iov_base = ln;
	req->iovs[2].iov_len = 2;
	req->iovs[3].iov_base = key->iov_base;
	req->iovs[3].iov_len = key->iov_len;
	req->iovs[4].iov_base = ln;
	req->iovs[4].iov_len = 2;

	if (drand48() > info->get_ratio) {
		val_len = lround(generate(info->val_len));
		assert(val_len <= MAX_VAL_SIZE);

		req->iovs[0].iov_base = set_prem;
		req->iovs[0].iov_len = 14;

		req->iovs[5].iov_base = dollar;
		req->iovs[5].iov_len = 1;

		// Fix val
		sprintf(val_len_str, "%ld", val_len);
		req->iovs[6].iov_base = val_len_str;
		req->iovs[6].iov_len = strlen(val_len_str);
		req->iovs[7].iov_base = ln;
		req->iovs[7].iov_len = 2;
		req->iovs[8].iov_base = random_char;
		req->iovs[8].iov_len = val_len;
		req->iovs[9].iov_base = ln;
		req->iovs[9].iov_len = 2;

		req->iov_cnt = 10;
	} else {
		req->iovs[0].iov_base = get_prem;
		req->iovs[0].iov_len = 14;

		req->iov_cnt = 5;
	}

	return 0;
}

int redis_get_key_count(struct application_protocol *proto)
{
	struct kv_info *info;

	info = (struct kv_info *)proto->arg;
	return info->key->key_count;
}

int redis_init(char *proto, struct application_protocol *app_proto)
{
	struct kv_info *data;
	char *token, *key_dist;
	int key_count;
	char *saveptr;
	char key_sel[64];

	data = malloc(sizeof(struct kv_info));
	assert(data != NULL);

	assert(strncmp("redis", proto, 5) == 0);

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

	app_proto->type = PROTO_REDIS;
	app_proto->arg = data;
	app_proto->consume_response = redis_consume_response;
	app_proto->create_request = redis_create_request;

	return 0;
}
