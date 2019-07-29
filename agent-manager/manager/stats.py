# MIT License
#
# Copyright (c) 2019-2021 Ecole Polytechnique Federale Lausanne (EPFL)
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
import numpy
import math
from scipy.stats import spearmanr, anderson, kstest, ks_2samp
from statsmodels.tsa.stattools import adfuller

from manager.agentcontroller import MAX_PER_THREAD_SAMPLES, MAX_PER_THREAD_TX_SAMPLES

IID_A_VAL = 1e-10

class LancetThroughputStats:
    def __init__(self):
        self.RxBytes  = 0
        self.RxReqs   = 0
        self.TxBytes  = 0
        self.TxReqs   = 0
        self.ia_is_correct = False

class LancetLatencyStats:
    def _init_(self):
        self.throughput_stats = LancetThroughputStats()
        self.Avg_latency = 0
        self.P50i = 0
        self.P50 = 0
        self.P50k = 0
        self.P90i = 0
        self.P90 = 0
        self.P90k = 0
        self.P95i = 0
        self.P95 = 0
        self.P95k = 0
        self.P99i = 0
        self.P99 = 0
        self.P99k = 0
        self.IsIID = 0
        self.ToReduce = 0

def get_ci(samples, percentile):
    size = len(samples)
    heta = 1.96 # for 95th confidence
    prod = size*percentile
    j = math.floor(prod - heta*math.sqrt(prod*(1-percentile)))
    k = math.ceil(prod + heta*math.sqrt(prod*(1-percentile))) + 1
    j, k = int(j), int(k)
    j, k = [int(samples[x]) if 0 <= x < size else 0 for x in [j, k]]
    return j, k

def check_iid(all_latency_stats, per_thread_samples):
    if (len(all_latency_stats) == 0):
        print("No latency stats")
        return False, -1
    lags = [2,5,10,25,50,100, 200, 500, 1000]
    data = []
    for s in all_latency_stats:
        sample_count = min(per_thread_samples, s.IncIdx)
        data += map(lambda x: (x.nsec_latency, x.sec_send*1e9+x.nsec_send),
                s.Samples[:sample_count])
    data.sort(key=lambda x: x[1])
    latencies = list(map(lambda x: x[0], data))
    corr, p = spearmanr(latencies[:-1], latencies[1:])
    print("Spearman {} {}".format(corr, p))

    if p > IID_A_VAL:
        # Data are iid
        return True, 0
    for lag in lags:
        corr, p = spearmanr(latencies[:-lag], latencies[lag:])
        if p > IID_A_VAL:
            break

    return False, lag

def check_interarrival(all_stats):
    all_tx_samples = []
    for s in all_stats:
        sample_count = min(MAX_PER_THREAD_TX_SAMPLES, s.TxTs.Count)
        all_tx_samples += map(lambda x: x.sec*1e9+x.nsec, s.TxTs.Samples[:sample_count])

    if len(all_tx_samples) == 0:
        print("No tx samples")
        return False
    res = anderson(all_tx_samples, dist='expon')
    print(res)
    return res[0] < 2*res[1][4]

def check_stationarity(all_latency_stats, per_thread_samples):
    data = []
    for s in all_latency_stats:
        sample_count = min(per_thread_samples, s.IncIdx)
        data += map(lambda x: (x.nsec_latency, x.sec_send*1e9+x.nsec_send),
                s.Samples[:sample_count])
    data.sort(key=lambda x: x[1])
    latencies = list(map(lambda x: x[0], data))

    adf_res = adfuller(latencies)
    return adf_res[0] < 0

def aggregate_throughput(stats):
    agg = LancetThroughputStats()
    for s in stats:
        agg.RxBytes +=  s.RxBytes
        agg.RxReqs  +=  s.RxReqs
        agg.TxBytes +=  s.TxBytes
        agg.TxReqs  +=  s.TxReqs

    agg.ia_is_correct = check_interarrival(stats)

    return agg

def aggregate_latency(stats, per_thread_samples):
    agg = LancetLatencyStats()
    agg.throughput_stats = aggregate_throughput(stats)

    all_samples = []
    for s in stats:
        sample_count = min(per_thread_samples, s.IncIdx)
        all_samples += map(lambda x: x.nsec_latency, s.Samples[:sample_count])

    # Uncomment in order to collect samples
    #with open("/tmp/kogias/lancet-samples", 'w') as f:
    #    f.write("\n".join(map(str, all_samples)))

    print("There are {} samples".format(len(all_samples)))
    all_samples.sort()
    agg.Avg_latency = int(numpy.mean(all_samples))
    agg.P50 = int(numpy.percentile(all_samples, 50))
    agg.P50i, agg.P50k = get_ci(all_samples, 0.5)
    agg.P90 = int(numpy.percentile(all_samples, 90))
    agg.P90i, agg.P90k = get_ci(all_samples, 0.9)
    agg.P95 = int(numpy.percentile(all_samples, 95))
    agg.P95i, agg.P95k = get_ci(all_samples, 0.95)
    agg.P99 = int(numpy.percentile(all_samples, 99))
    agg.P99i, agg.P99k = get_ci(all_samples, 0.99)
    agg.is_stationary = check_stationarity(stats, per_thread_samples)
    is_iid, to_reduce = check_iid(stats, per_thread_samples)
    agg.IsIID = is_iid
    agg.ToReduce = to_reduce

    return agg
