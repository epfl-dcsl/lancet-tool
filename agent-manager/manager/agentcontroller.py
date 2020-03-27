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
import subprocess
import time
import posix_ipc
import mmap
import ctypes
import shlex

import logging
logging.basicConfig()
log = logging.getLogger(__file__)
log.setLevel(logging.DEBUG)

MAX_PER_THREAD_SAMPLES = 131072
MAX_PER_THREAD_TX_SAMPLES = 4096

class AgentControlBlock(ctypes.Structure):
    _pack_ = 1
    _fields_ = [
        ('idist', ctypes.c_char * 52),
        ('should_load', ctypes.c_int),
        ('should_measure', ctypes.c_int),
        ('thread_count', ctypes.c_int),
        ('agent_type', ctypes.c_int),
        ('sample_count', ctypes.c_uint32),
        ('sampling_rate', ctypes.c_double),
        ('conn_open', ctypes.c_int),
    ]

class Timespec(ctypes.Structure):
    _pack_ = 1
    _fields_ = [
        ('sec', ctypes.c_uint64),
        ('nsec', ctypes.c_uint64),
    ]

class TxTimestamps(ctypes.Structure):
    _pack_ = 1
    _fields_ = [
        ('Count', ctypes.c_uint32),
        ('Samples', Timespec * MAX_PER_THREAD_SAMPLES)
    ]

class ThroughputStats(ctypes.Structure):
    _pack_ = 1
    _fields_ = [
        ('RxBytes', ctypes.c_uint64),
        ('RxReqs', ctypes.c_uint64),
        ('TxBytes', ctypes.c_uint64),
        ('TxReqs', ctypes.c_uint64),
        ('TxTs', TxTimestamps),
    ]


class LatSample(ctypes.Structure):
    _pack_ = 1
    _fields_ = [
        ('nsec_latency', ctypes.c_uint64),
        ('sec_send', ctypes.c_uint64),
        ('nsec_send', ctypes.c_uint64),
    ]

class LatencyStats(ctypes.Structure):
    _pack_ = 1
    _fields_ = [
        ('RxBytes', ctypes.c_uint64),
        ('RxReqs', ctypes.c_uint64),
        ('TxBytes', ctypes.c_uint64),
        ('TxReqs', ctypes.c_uint64),
        ('IncIdx', ctypes.c_uint32),
        ('Samples', LatSample * MAX_PER_THREAD_SAMPLES),
        ('TxTs', TxTimestamps),
    ]

class LancetController:

    def __init__(self, librand_path):
        assert librand_path.exists() and librand_path.is_file(), "Bad librand path at {}".format(librand_path)
        extc = ctypes.CDLL(librand_path.absolute().as_posix())
        self.set_load_fn = extc.set_avg_ext
        self.thread_stats = []

    def launch_agent(self, args):
        launch_args = [str(args.agent.as_posix())] + shlex.split(" ".join(args.agent_args))
        log.debug("Agent launch command: \"{}\"".format(launch_args))
        self.agent = subprocess.Popen(launch_args)
        shm = None
        for i in range(10):
            time.sleep(1)
            try:
                shm = posix_ipc.SharedMemory('/lancetcontrol', 0)
            except posix_ipc.ExistentialError:
                continue
            break
        assert shm is not None
        buffer = mmap.mmap(shm.fd, ctypes.sizeof(AgentControlBlock),
                mmap.MAP_SHARED, mmap.PROT_WRITE)
        self.acb = AgentControlBlock.from_buffer(buffer)
        # Map the stats
        if self.acb.agent_type == 0:
            for i in range(self.acb.thread_count):
                shm = posix_ipc.SharedMemory('/lancet-stats{}'.format(i), 0)
                buffer = mmap.mmap(shm.fd, ctypes.sizeof(ThroughputStats),
                        mmap.MAP_SHARED, mmap.PROT_WRITE)
                self.thread_stats.append(ThroughputStats.from_buffer(buffer))
        elif (self.acb.agent_type == 1 or  self.acb.agent_type == 2 or self.acb.agent_type == 3):
            for i in range(self.acb.thread_count):
                shm = posix_ipc.SharedMemory('/lancet-stats{}'.format(i), 0)
                buffer = mmap.mmap(shm.fd, ctypes.sizeof(LatencyStats),
                        mmap.MAP_SHARED, mmap.PROT_WRITE)
                self.thread_stats.append(LatencyStats.from_buffer(buffer))
        else:
            assert False


    def start_load(self, load):
        self.acb.should_measure = 0
        per_thread_load = float(load.value) / self.acb.thread_count
        l = ctypes.c_double(1e6 / per_thread_load)
        self.set_load_fn(ctypes.byref(self.acb), l)
        self.acb.should_load = 1

    def start_measure(self, sample_count, sampling_rate):
        self.clear_stats()
        print("Will start load for {} samples with {} sampling rate".format(sample_count, sampling_rate))
        self.acb.sample_count = int(sample_count / self.acb.thread_count)
        self.acb.sampling_rate = sampling_rate / 100.0
        self.acb.should_measure = 1

    def get_conn_open(self):
        return self.acb.conn_open

    def terminate(self):
        self.agent.kill()

    def get_stats(self):
        self.acb.should_measure = 0
        return self.thread_stats

    def check_agent(self):
        return self.agent.poll()

    def get_per_thread_samples(self):
        return self.acb.sample_count

    def clear_stats(self):
        for stats in self.thread_stats:
            stats.RxBytes = 0
            stats.RxReqs = 0
            stats.TxBytes = 0
            stats.TxReqs = 0
            stats.TxTs.Count = 0

            if self.acb.agent_type > 0: # clear latency stats
                stats.IncIdx = 0
