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
#!/usr/bin/env python3

# import sys
import socket
import time
import argparse
import pathlib

from manager.proto import LancetProto
from manager.agentcontroller import LancetController
from manager.stats import aggregate_throughput, aggregate_latency

MANAGER_PORT = 5001
this_dir = pathlib.Path(__file__).absolute().parent
default_binary_asset_dir = this_dir / "assets/"
agent_bin_name = "agent"
lib_rand_name = "librand.so"

def get_args():
    parser = argparse.ArgumentParser(
        description="The Lancet Agent"
    )
    parser.add_argument("-b", "--binary-assets", default=default_binary_asset_dir,
                        help="default base directory for binary assets")
    parser.add_argument("--agent", help="override for custom path to args")
    parser.add_argument("--rand", help="override for custom path to librand.so")
    parser.add_argument("agent_args", nargs="*", help="arguments to directly pass into the agent")
    args = parser.parse_args()

    ba = args.binary_assets
    if not (ba.exists() and ba.is_dir()):
        raise Exception("Could not find binary assets base directory at {}".format(ba))
    if args.agent is None:
        args.agent = ba / agent_bin_name
    agent = args.agent
    if not (agent.exists() and agent.is_file()):
        raise Exception("Bad agent path at {}".format(agent))
    if args.rand is None:
        args.rand = ba / lib_rand_name
    librand = args.rand
    if not (librand.exists() and librand.is_file()):
        raise Exception("Bad librand.so path at {}".format(librand))
    return args

class LancetServer:
    def __init__(self, librand):
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.socket.bind(("", MANAGER_PORT))
        self.socket.listen(1)
        self.controller = LancetController(librand_path=librand)

    def run(self, args):
        self.controller.launch_agent(args)
        conn, addr = self.socket.accept()
        self.proto = LancetProto(conn)
        while 1:
            msg = self.proto.recv_msg()
            res = self.process_msg(msg)
            if res == -1:
                print("Lancet will terminate")
                ret = self.controller.check_agent()
                if ret:
                    print("Agent failed with {}".format(ret))
                else:
                    print("Agent is running")
                break
        self.proto.close()
        self.controller.terminate()

    def process_msg(self, msg):
        if msg.msg_type == 0:
            self.controller.start_load(msg.info)
        elif msg.msg_type == 1:
            self.controller.start_measure(msg.info.SampleCount, msg.info.SamplingRate)
            self.start_time = time.time()
            # Configure sampling
        elif msg.msg_type == 2:
            if msg.info == 0:
                self.end_time = time.time()
                throughput_stats = self.controller.get_stats()
                agg_stats = aggregate_throughput(throughput_stats)
                agg_stats.duration = self.end_time - self.start_time
                self.proto.reply_throughput(agg_stats)
            elif msg.info == 1:
                self.end_time = time.time()
                latency_stats = self.controller.get_stats()
                try:
                    agg_stats = aggregate_latency(latency_stats,
                            self.controller.get_per_thread_samples())
                except ValueError:
                    return -1
                agg_stats.duration = self.end_time - self.start_time
                self.proto.reply_latency(agg_stats) # should pass something here
            else:
                print("Unknown report msg")
                return -1
        elif msg.msg_type == 3:
            self.proto.reply_value(self.controller.get_conn_open())
        elif msg.msg_type == -1:
            return -1
        return 0


def main():
    args = get_args()
    server = LancetServer(librand=args.rand)
    server.run(args=args)
    # try:
    # server.run()
    # except:
    # server.proto.close()
    # self.controller.terminate()

if __name__ == "__main__":
    main()
