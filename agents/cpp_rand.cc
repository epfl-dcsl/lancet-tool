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
 * The above copyright notice and this permission notice shall be included in all
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
#include <random>
#include <cmath>

#include <lancet/cpp_rand.h>

extern "C" {
	struct cpp_gen *new_normal_gen() {
		std::random_device rd {};
		std::mt19937 *gen = new std::mt19937(rd());
		std::normal_distribution<double> *d = new std::normal_distribution<double>();
		struct cpp_gen *ng = new struct cpp_gen;
		ng->d = d;
		ng->gen = gen;

		return ng;
	}

	double get_normal_rand(struct cpp_gen *ng) {
		std::normal_distribution<double> *d = (std::normal_distribution<double> *)ng->d;
		std::mt19937 *gen = (std::mt19937 *)ng->gen;

		return (*d)(*gen);
	}

	struct cpp_gen *new_gamma_gen(double alpha, double beta) {
		std::random_device rd {};
		std::mt19937 *gen = new std::mt19937(rd());
		//std::default_random_engine *gen = new std::default_random_engine;

		std::gamma_distribution<double> *d = new std::gamma_distribution<double>(alpha, beta);
		struct cpp_gen *gg = new struct cpp_gen;
		gg->d = d;
		gg->gen = gen;
		return gg;

	}
	double get_gamma_rand(struct cpp_gen *gg) {
		std::gamma_distribution<double> *d  = (std::gamma_distribution<double> *)gg->gen;
		std::mt19937 *gen = (std::mt19937 *)gg->gen;
		return (*d)(*gen);
	}
}
