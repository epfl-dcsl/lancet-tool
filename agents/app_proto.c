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
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/uio.h>

#include <lancet/app_proto.h>
#include <lancet/error.h>
#include <lancet/rand_gen.h>

__thread void *per_thread_arg = NULL;
char random_char[2 * 1024 * 1024];

/*
 * Echo protocol
 */
int echo_create_request(struct application_protocol *proto, struct request *req)
{
	struct iovec *fixed_req = (struct iovec *)proto->arg;
	req->iovs[0] = *fixed_req;
	req->iov_cnt = 1;
	req->meta = NULL;

	return 0;
}

struct byte_req_pair echo_consume_response(struct application_protocol *proto,
										   struct iovec *response)
{
	struct byte_req_pair res;

	struct iovec *msg = (struct iovec *)proto->arg;

	res.reqs = response->iov_len / msg->iov_len;
	res.bytes = res.reqs * msg->iov_len;

	return res;
}

static int echo_init(char *proto, struct application_protocol *app_proto)
{
	char *token;
	struct iovec *arg;
	int message_len;

	token = strtok(proto, ":");
	token = strtok(NULL, ":");

	message_len = atoi(token);
	arg = malloc(sizeof(struct iovec));
	assert(arg);
	arg->iov_base = malloc(message_len);
	assert(arg->iov_base);
	memset(arg->iov_base, '#', message_len);
	arg->iov_len = message_len;

	app_proto->type = PROTO_ECHO;
	// The proto arg is the iovec with the message
	app_proto->arg = arg;
	app_proto->create_request = echo_create_request;
	app_proto->consume_response = echo_consume_response;

	return 0;
}

/*
 * Synthetic protocol
 */
int synthetic_create_request(struct application_protocol *proto,
							 struct request *req)
{
	struct rand_gen *generator = (struct rand_gen *)proto->arg;

	if (!per_thread_arg) {
		per_thread_arg = malloc(sizeof(long));
		assert(per_thread_arg);
	}
	per_thread_arg = (void *)lround(generate(generator));
	req->iovs[0].iov_base = &per_thread_arg;
	req->iovs[0].iov_len = sizeof(long);
	req->iov_cnt = 1;
	req->meta = NULL;

	return 0;
}

int rep_synth_create_request(struct application_protocol *proto,
							 struct request *req)
{
	long * rep_arg = (long *)proto->arg;
	struct rand_gen *generator = (struct rand_gen *)rep_arg[0];

	if (!per_thread_arg) {
		per_thread_arg = malloc(sizeof(long));
		assert(per_thread_arg);
	}
	per_thread_arg = (void *)lround(generate(generator));
	req->iovs[0].iov_base = &per_thread_arg;
	req->iovs[0].iov_len = sizeof(long);
	req->iov_cnt = 1;

	if (drand48()*100 <= rep_arg[1])
		req->meta = (void *)(unsigned long)3; // replicated route no side effects
	else
		req->meta = (void *)(unsigned long)2; // replicated route no side effects

	return 0;
}


struct byte_req_pair
synthetic_consume_response(struct application_protocol *proto,
						   struct iovec *response)
{
	struct byte_req_pair res;

	res.reqs = response->iov_len / sizeof(long);
	res.bytes = res.reqs * sizeof(long);
	return res;
}

static int synthetic_init(char *proto, struct application_protocol *app_proto)
{
	char *token;
	struct rand_gen *gen = NULL;
	long *rep_arg;

	// Remove the type.
	token = strtok(proto, ":");
	token = strtok(NULL, "");

	gen = init_rand(token);
	assert(gen);

	if (strncmp(proto, "rep", 3) == 0) {
		rep_arg = malloc(2*sizeof(long));
		assert(rep_arg);
		rep_arg[0] = (long) gen;
		token = strtok(NULL, "");
		rep_arg[1] = atof(token);
		printf("Percentage of NO_SIDE_EFFECT is %ld\n", rep_arg[1]);
		app_proto->arg = rep_arg;
		app_proto->type = PROTO_REP_SYNTHETIC;
		app_proto->create_request = rep_synth_create_request;
	} else {
		app_proto->type = PROTO_SYNTHETIC;
		app_proto->arg = gen;
		app_proto->create_request = synthetic_create_request;
	}
	app_proto->consume_response = synthetic_consume_response;

	return 0;
}

struct application_protocol *init_app_proto(char *proto)
{
	struct application_protocol *app_proto;

	app_proto = malloc(sizeof(struct application_protocol));
	assert(app_proto);

	// Init random char
	memset(random_char, 'x', MAX_VAL_SIZE);

	if (strncmp(proto, "echo", 4) == 0)
		echo_init(proto, app_proto);
	else if (strncmp(proto, "synthetic", 9) == 0)
		synthetic_init(proto, app_proto);
	else if (strncmp(proto, "redis", 5) == 0)
		redis_init(proto, app_proto);
	else if (strncmp(proto, "memcache", 8) == 0)
		memcache_init(proto, app_proto);
	else if (strncmp(proto, "http", 4) == 0) {
		int i = http_proto_init(proto, app_proto);
		if (i != 0) {
			return NULL;
		}
	} else if (strncmp(proto, "rep-synth", 9) == 0)
		synthetic_init(proto, app_proto);
	else {
		lancet_fprintf(stderr, "Unknown application protocol\n");
		return NULL;
	}

	return app_proto;
}
