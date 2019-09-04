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
#include <string.h>

#include <lancet/cpp_rand.h>
#include <lancet/error.h>
#include <lancet/rand_gen.h>

/*
 * Deterministic distribution
 * params holds the number to return
 */

static __thread uint64_t prev_rr;

static double fixed_inv_cdf(struct rand_gen *gen,
							__attribute__((unused)) double y)
{
	return gen->params.p1.a;
}

static void fixed_set_avg(struct rand_gen *gen, double avg)
{
	gen->params.p1.a = avg;
}

static void fixed_init(struct rand_gen *gen, struct param_1 *param)
{
	gen->set_avg = fixed_set_avg;
	gen->inv_cdf = fixed_inv_cdf;
	gen->generate = NULL;
	gen->gen_type = GEN_FIXED;

	gen->set_avg(gen, param->a);
	free(param);
}

/*
 * Round robin
 * param holds the max
 */
static double rr_generate(struct rand_gen *gen)
{
	uint64_t max = gen->params.p1.a;

	return prev_rr++ % max;
}

static void rr_init(struct rand_gen *gen, struct param_1 *param)
{
	gen->generate = rr_generate;
	gen->params.p1.a = param->a;

	free(param);
}

/*
 * Uniform random
 * param holds the max
 */
static double uni_generate(struct rand_gen *gen)
{
	uint64_t max = gen->params.p1.a;

	return rand() % max;
}

static void uni_init(struct rand_gen *gen, struct param_1 *param)
{
	gen->generate = uni_generate;
	gen->params.p1.a = param->a;

	free(param);
}

/*
 * Exponential distribution
 * params holds lambda (1/avg)
 */

static double exp_inv_cdf(struct rand_gen *gen, double y)
{
	return -log(y) / gen->params.p1.a;
}

static void exp_set_avg(struct rand_gen *gen, double avg)
{
	gen->params.p1.a = (double)1.0 / avg;
}

static void exp_init(struct rand_gen *gen, struct param_1 *param)
{
	gen->set_avg = exp_set_avg;
	gen->inv_cdf = exp_inv_cdf;
	gen->generate = NULL;
	gen->gen_type = GEN_EXP;

	gen->set_avg(gen, param->a);
	free(param);
}

/*
 * Generalised pareto distribution
 * params hold a struct of param_lss with loc, shape and scale
 */
static double gpar_inv_cdf(struct rand_gen *gen, double y)
{
	return gen->params.lss.loc +
		   gen->params.lss.scale * (pow(1 - y, -gen->params.lss.shape) - 1) /
			   gen->params.lss.shape;
}

/*
 * Change only the scale parameter (legacy)
 */
static void gpar_set_avg(struct rand_gen *gen, double avg)
{
	gen->params.lss.scale =
		(avg - gen->params.lss.loc) * (1 - gen->params.lss.shape);
}

static void gpar_init(struct rand_gen *gen, struct param_3 *param)
{
	gen->params.p3 = *param;
	gen->set_avg = gpar_set_avg;
	gen->inv_cdf = gpar_inv_cdf;
	gen->generate = NULL;
	gen->gen_type = GEN_OTHER;

	free(param);
}

/*
 * GEV distribution
 * params holds a struct param_lss
 */
static double gev_inv_cdf(struct rand_gen *gen, double y)
{
	return gen->params.lss.loc +
		   gen->params.lss.scale * (pow(-exp(y), -gen->params.lss.shape) - 1) /
			   gen->params.lss.shape;
}

/*
 * Not implemented
 */
static void gev_set_avg(__attribute__((unused)) struct rand_gen *gen,
						__attribute__((unused)) double avg)
{
	assert(0);
}

static void gev_init(struct rand_gen *gen, struct param_3 *param)
{
	gen->params.p3 = *param;
	gen->set_avg = gev_set_avg;
	gen->inv_cdf = gev_inv_cdf;
	gen->generate = NULL;
	gen->gen_type = GEN_OTHER;
}

/*
 * Bimodal distribution.
 * maxi1:maxi2:Prob1
 */
static void bimodal_set_avg(__attribute__((unused)) struct rand_gen *gen,
							__attribute__((unused)) double avg)
{
	// Should never be called.
	assert(0);
}

static double bimodal_inv_cdf(struct rand_gen *gen, double y)
{
	if (y <= gen->params.bp.prob)
		return gen->params.bp.low;
	return gen->params.bp.up;
}

static void bimodal_init(struct rand_gen *gen, struct param_3 *param)
{
	gen->params.p3 = *param;
	gen->set_avg = bimodal_set_avg;
	gen->inv_cdf = bimodal_inv_cdf;
	gen->generate = NULL;
	gen->gen_type = GEN_OTHER;
	free(param);
}

/*
 * Lognormal distribution
 */
static double lognorm_generate(struct rand_gen *gen)
{
	double y = get_normal_rand(gen->params.lgp.ng);
	return exp(gen->params.lgp.mu + y * gen->params.lgp.sigma);
}

static double lognorm_inv_cdf(struct rand_gen *gen, double y)
{
	assert(0);
}

static void lognorm_set_avg(__attribute__((unused)) struct rand_gen *gen,
							double avg)
{
	assert(0);
}

static void lognormal_init(struct rand_gen *gen, struct param_2 *param)
{

	gen->params.lgp.ng = new_normal_gen();
	gen->params.lgp.mu = param->a;
	gen->params.lgp.sigma = param->b;

	gen->generate = lognorm_generate;
	gen->set_avg = lognorm_set_avg;
	gen->inv_cdf = lognorm_inv_cdf;
	gen->gen_type = GEN_OTHER;
	free(param);
}

/*
 * Gamma distribution
 */
static double gamma_generate(struct rand_gen *gen)
{
	int res = get_gamma_rand(gen->params.gp.gg);
	return res;
}

static double gamma_inv_cdf(struct rand_gen *gen, double y)
{
	assert(0);
}

static void gamma_set_avg(__attribute__((unused)) struct rand_gen *gen,
						  double avg)
{
	assert(0);
}

static void gamma_init(struct rand_gen *gen, struct param_2 *param)
{
	assert(0);

	gen->params.gp.gg = new_gamma_gen(param->a, param->b);

	gen->generate = gamma_generate;
	gen->set_avg = gamma_set_avg;
	gen->inv_cdf = gamma_inv_cdf;
	gen->gen_type = GEN_OTHER;
	free(param);
}

static struct param_1 *parse_param_1(char *type)
{
	char *tok;
	struct param_1 *res;

	res = (struct param_1 *)malloc(sizeof(struct param_1));
	assert(res);

	tok = strtok(type, ":");
	tok = strtok(NULL, ":");
	res->a = (tok == NULL) ? 0 : atof(tok);

	return res;
}

static struct param_2 *parse_param_2(char *type)
{
	char *tok;
	struct param_2 *params;

	params = (struct param_2 *)malloc(sizeof(struct param_2));
	assert(params);

	tok = strtok(type, ":");
	tok = strtok(NULL, ":");
	params->a = atof(tok);
	tok = strtok(NULL, ":");
	params->b = atof(tok);

	return params;
}

static struct param_3 *parse_param_3(char *type)
{
	char *tok;
	struct param_3 *params;

	params = (struct param_3 *)malloc(sizeof(struct param_3));
	assert(params);

	tok = strtok(type, ":");
	tok = strtok(NULL, ":");
	params->a = atof(tok);
	tok = strtok(NULL, ":");
	params->b = atof(tok);
	tok = strtok(NULL, ":");
	params->c = atof(tok);

	return params;
}

struct rand_gen *init_rand(char *gen_type)
{
	struct rand_gen *gen = (struct rand_gen *)malloc(sizeof(struct rand_gen));
	assert(gen);

	if (strncmp(gen_type, "fixed", 5) == 0)
		fixed_init(gen, parse_param_1(gen_type));
	else if (strncmp(gen_type, "rr", 2) == 0)
		rr_init(gen, parse_param_1(gen_type));
	else if (strncmp(gen_type, "uni", 3) == 0)
		uni_init(gen, parse_param_1(gen_type));
	else if (strncmp(gen_type, "exp", 3) == 0)
		exp_init(gen, parse_param_1(gen_type));
	else if (strncmp(gen_type, "pareto", 6) == 0)
		gpar_init(gen, parse_param_3(gen_type));
	else if (strncmp(gen_type, "gev", 3) == 0)
		gev_init(gen, parse_param_3(gen_type));
	else if (strcmp(gen_type, "fb_key") == 0) {
		char type[] = "gev:30.7984:8.20449:0.078688";
		gev_init(gen, parse_param_3(type));
	} else if (strcmp(gen_type, "fb_ia") == 0) {
		char type[] = "gpar:0:16.0292:0.154971";
		gpar_init(gen, parse_param_3(type));
	} else if (strcmp(gen_type, "fb_val") == 0) {
		/* WARNING: this is not exactly the same as mutilate */
		char type[] = "gpar:15.0:214.476:0.348238";
		gpar_init(gen, parse_param_3(type));
	} else if (strncmp(gen_type, "bimodal", 7) == 0)
		bimodal_init(gen, parse_param_3(gen_type));
	else if (strncmp(gen_type, "lognorm", 7) == 0)
		lognormal_init(gen, parse_param_2(gen_type));
	else if (strncmp(gen_type, "gamma", 5) == 0)
		gamma_init(gen, parse_param_2(gen_type));
	else {
		lancet_fprintf(stderr, "Unknown generator type %s\n", gen_type);
		return NULL;
	}
	return gen;
}

void set_avg_ext(struct rand_gen *gen, double avg)
{
	switch (gen->gen_type) {
	case GEN_FIXED:
		fixed_set_avg(gen, avg);
		break;
	case GEN_EXP:
		exp_set_avg(gen, avg);
		break;
	default:
		assert(0);
	}
}
