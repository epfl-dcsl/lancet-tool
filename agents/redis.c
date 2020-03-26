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

#ifdef ENABLE_R2P2
#include <r2p2/api.h>
#endif

#define YCSBE_SCAN_RATIO 0.95
#define YCSBE_INSERT_RATIO 0.05
#define YCSBE_KEY_COUNT 1000000
#define YCSBE_MAX_SCAN_LEN 10
#define YCSBE_FIELD_COUNT 10
#define YCSBE_FIELD_SIZE 100

struct ycsbe_info {
	double scan_ratio;
	double insert_ratio;
	int key_count;
	int scan_len;
	int field_count;
	int field_size;
	int replicated;
	char *fixed_req_body;
};

static char set_prem[] = "*3\r\n$3\r\nSET\r\n$";
static char get_prem[] = "*2\r\n$3\r\nGET\r\n$";
static char ln[] = "\r\n";
static char dollar[] = "$";
static char ycsbe_insert_prem[] = "ycsbe.insert ";
static char ycsbe_scan_prem[] = "ycsbe.scan ";
static __thread char ycsbe_key[64];
static __thread char ycsbe_scan[64];
static __thread char key_len_str[64];
static __thread char val_len_str[64];

static int parse_string(char *buf, int bytes_left)
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

static int parse_bulk_string(char *buf, int bytes_left)
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

static struct byte_req_pair redis_kv_consume_response(struct application_protocol *proto,
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

static int redis_kv_create_request(struct application_protocol *proto,
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
#ifdef ENABLE_R2P2
		req->meta = (void *)(unsigned long)FIXED_ROUTE;
#endif
	} else {
		req->iovs[0].iov_base = get_prem;
		req->iovs[0].iov_len = 14;

		req->iov_cnt = 5;
#ifdef ENABLE_R2P2
		req->meta = (void *)(unsigned long)LB_ROUTE;
#endif
	}

	return 0;
}

static int init_redis_kv(char *proto, struct application_protocol *app_proto)
{
	struct kv_info *data;
	char *token, *key_dist;
	int key_count;
	char *saveptr;
	char key_sel[64];

	data = malloc(sizeof(struct kv_info));
	assert(data != NULL);

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
	app_proto->consume_response = redis_kv_consume_response;
	app_proto->create_request = redis_kv_create_request;

	return 0;
}

static int redis_ycsbe_create_request(struct application_protocol *proto,
						 struct request *req)
{
	int key, scan_count;
	struct ycsbe_info *info;

	info = (struct ycsbe_info *)proto->arg;

	key = rand() % info->key_count;
	sprintf(ycsbe_key, "%d ", key);

	if (drand48()<=info->scan_ratio) {
		// perform a scan
		scan_count = rand() % info->scan_len + 1;
		sprintf(ycsbe_scan, "%d\n", scan_count);

		req->iovs[0].iov_base = ycsbe_scan_prem;
		req->iovs[0].iov_len = 11;
		req->iovs[1].iov_base = ycsbe_key;
		req->iovs[1].iov_len = strlen(ycsbe_key);
		req->iovs[2].iov_base = ycsbe_scan;
		req->iovs[2].iov_len = strlen(ycsbe_scan);
		req->iov_cnt = 3;

		if (info->replicated) {
#ifdef ENABLE_R2P2
			req->meta = (void *)(unsigned long)REPLICATED_ROUTE_NO_SE;
#else
			assert(0);
#endif
		} else
			req->meta = NULL;
	} else {
		// perform an insert
		req->iovs[0].iov_base = ycsbe_insert_prem;
		req->iovs[0].iov_len = 13;
		req->iovs[1].iov_base = ycsbe_key;
		req->iovs[1].iov_len = strlen(ycsbe_key);
		req->iovs[2].iov_base = info->fixed_req_body;
		req->iovs[2].iov_len = info->field_count*(info->field_size+1);
		req->iov_cnt = 3;
		if (info->replicated) {
#ifdef ENABLE_R2P2
			req->meta = (void *)(unsigned long)REPLICATED_ROUTE;
#else
			assert(0);
#endif

		} else
			req->meta = NULL;
	}

	return 0;
}

static int init_redis_ycsbe(char *proto, struct application_protocol *app_proto)
{
	struct ycsbe_info *yinfo;
	int i;
	char *body;

	yinfo = malloc(sizeof(struct ycsbe_info));
	assert(yinfo);

	yinfo->scan_ratio = YCSBE_SCAN_RATIO;
	yinfo->insert_ratio = YCSBE_INSERT_RATIO;
	yinfo->key_count = YCSBE_KEY_COUNT;
	yinfo->scan_len = YCSBE_MAX_SCAN_LEN;
	yinfo->field_count = YCSBE_FIELD_COUNT;
	yinfo->field_size = YCSBE_FIELD_SIZE;
	yinfo->fixed_req_body = malloc(yinfo->field_count*(yinfo->field_size+1));
	assert(yinfo->fixed_req_body);
	if (strncmp("redis-ycsber", proto, 12) == 0)
		yinfo->replicated = 1;
	else
		yinfo->replicated = 0;

	body = yinfo->fixed_req_body;
	for (i=0;i<yinfo->field_count;i++) {
		memset(body, 'x', yinfo->field_size);
		body += yinfo->field_size;
		*body++ = ' ';
	}
	*(body-1) = '\n';

	app_proto->type = PROTO_REDIS_YCSBE;
	app_proto->arg = yinfo;
	app_proto->consume_response = NULL;
	app_proto->create_request = redis_ycsbe_create_request;

	return 0;
}

int redis_init(char *proto, struct application_protocol *app_proto)
{
	assert(strncmp("redis", proto, 5) == 0);

	if (strncmp("redis-ycsbe", proto, 11) == 0)
		init_redis_ycsbe(proto, app_proto);
	else
		init_redis_kv(proto, app_proto);

	return 0;
}
