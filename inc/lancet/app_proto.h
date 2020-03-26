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

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/uio.h>

#include <lancet/key_gen.h>
#include <lancet/rand_gen.h>
#include <lancet/stats.h>

// A pointer to a random char for kv-store vals
#define MAX_VAL_SIZE 2 * 1024 * 1024
extern char random_char[MAX_VAL_SIZE];
extern __thread void *per_thread_arg;

#define MAX_IOVS 64
struct request {
	void *meta;
	int iov_cnt;
	struct iovec iovs[MAX_IOVS];
};

enum app_proto_type {
	PROTO_ECHO,
	PROTO_SYNTHETIC,
	PROTO_REDIS,
	PROTO_REDIS_YCSBE,
	PROTO_MEMCACHED_BIN,
	PROTO_MEMCACHED_ASCII,
	PROTO_HTTP,
	PROTO_STSS,
};

struct application_protocol {
	enum app_proto_type type;
	void *arg;
	int (*create_request)(struct application_protocol *proto,
						  struct request *req);
	struct byte_req_pair (*consume_response)(struct application_protocol *proto,
											 struct iovec *response);
};

struct application_protocol *init_app_proto(char *proto);
static inline int create_request(struct application_protocol *proto,
								 struct request *req)
{
	return proto->create_request(proto, req);
};

static inline struct byte_req_pair
consume_response(struct application_protocol *proto, struct iovec *response)
{
	return proto->consume_response(proto, response);
};

struct kv_info {
	struct key_gen *key;
	struct rand_gen *val_len;
	struct rand_gen *key_sel;
	double get_ratio;
};

static inline int kv_get_key_count(struct application_protocol *proto)
{
	struct kv_info *info;

	info = (struct kv_info *)proto->arg;
	return info->key->key_count;
}

/*
 * Redis
 * redis_<key_size_distr>_<val_size_distr>_<key_count>_<rw_ratio>_<key_selector>
 */
int redis_init(char *proto, struct application_protocol *app_proto);

/*
 * Memcached
 * <memcache-bin|memcache-ascii>_<key_size_distr>_<val_size_distr>_<key_count>_<rw_ratio>_<key_selector>
 */
int memcache_init(char *proto, struct application_protocol *app_proto);

/*
 * HTTP
 */
int http_proto_init(char *proto, struct application_protocol *app_proto);

#ifdef __cplusplus
};
#endif
