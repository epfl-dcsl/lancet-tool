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
import ctypes
import io

class MsgHdr(ctypes.Structure):
    _pack_ = 1
    _fields_ = [
        ('MessageType', ctypes.c_uint32),
        ('MessageLength', ctypes.c_uint32),
    ]

class Msg1(ctypes.Structure):
    _pack_ = 1
    _fields_ = [
        ('MessageType', ctypes.c_uint32),
        ('MessageLength', ctypes.c_uint32),
        ('Info', ctypes.c_uint32),
    ]

class StartLoadMsg(ctypes.Structure):
    _pack_ = 1
    _fields_ = [
        ('SampleCount', ctypes.c_uint32),
        ('SamplingRate', ctypes.c_double),
    ]

class ThroughputReply(ctypes.Structure):
    _pack_ = 1
    _fields_ = [
        ('RxBytes', ctypes.c_uint64),
        ('TxBytes', ctypes.c_uint64),
        ('ReqCount', ctypes.c_uint64),
        ('Duration', ctypes.c_uint64),
        ('CorrectIAD', ctypes.c_uint64),
    ]

class LatencyReply(ctypes.Structure):
    _pack_ = 1
    _fields_ = [
        ('Th_data', ThroughputReply),
        ('Avg_latency', ctypes.c_uint64),
        ('P50i', ctypes.c_uint64),
        ('P50', ctypes.c_uint64),
        ('P50k', ctypes.c_uint64),
        ('P90i', ctypes.c_uint64),
        ('P90', ctypes.c_uint64),
        ('P90k', ctypes.c_uint64),
        ('P95i', ctypes.c_uint64),
        ('P95', ctypes.c_uint64),
        ('P95k', ctypes.c_uint64),
        ('P99i', ctypes.c_uint64),
        ('P99', ctypes.c_uint64),
        ('P99k', ctypes.c_uint64),
        ('P999i', ctypes.c_uint64),
        ('P999', ctypes.c_uint64),
        ('P999k', ctypes.c_uint64),
        ('P9999i', ctypes.c_uint64),
        ('P9999', ctypes.c_uint64),
        ('P9999k', ctypes.c_uint64),
        ('P99999i', ctypes.c_uint64),
        ('P99999', ctypes.c_uint64),
        ('P99999k', ctypes.c_uint64),
        ('P999999i', ctypes.c_uint64),
        ('P999999', ctypes.c_uint64),
        ('P999999k', ctypes.c_uint64),
        ('ToReduceSampling', ctypes.c_uint32),
        ('IsIID', ctypes.c_uint8),
        ('IsStationary', ctypes.c_uint8),
    ]

class MsgInternal:
    def __init__(self, msg_type, info):
        self.msg_type = msg_type
        self.info = info

class LancetProto:
    def __init__(self, conn):
        self.conn = conn

    def recv_msg(self):
        data = self.conn.recv(8)
        if len(data) == 0:
            self.conn.close()
            lancet_msg = MsgInternal(-1, 0)
            return lancet_msg
        assert len(data) == 8
        buf = ctypes.create_string_buffer(data)
        msg = MsgHdr.from_buffer(buf)

        if msg.MessageType == 0: # START_LOAD
            data = self.conn.recv(4)
            assert len(data) == 4
            buf = ctypes.create_string_buffer(data)
            load = ctypes.c_uint32.from_buffer(buf)
            lancet_msg = MsgInternal(0, load)
            self.reply_ack()
        elif msg.MessageType == 1: # START_MEASURE
            data = self.conn.recv(12)
            assert len(data) == 12
            buf = ctypes.create_string_buffer(data)
            msg = StartLoadMsg.from_buffer(buf)
            lancet_msg = MsgInternal(1, msg)
            self.reply_ack()
        elif msg.MessageType == 2: # REPORT_REQ
            data = self.conn.recv(4)
            assert len(data) == 4
            buf = ctypes.create_string_buffer(data)
            report_type = ctypes.c_uint32.from_buffer(buf)
            lancet_msg = MsgInternal(2, report_type.value)
        elif msg.MessageType == 5: # CONN_OPEN
            data = self.conn.recv(4)
            assert len(data) == 4
            lancet_msg = MsgInternal(3, None)
        else:
            assert False
        return lancet_msg

    def reply_ack(self):
        msg = Msg1()
        msg.MessageType = 3 # Reply
        msg.MessageLength = 4
        msg.Info = 0 # REPLY_ACK
        self.conn.send(msg)

    def reply_value(self, value):
        msg = Msg1()
        msg.MessageType = 3 # Reply
        msg.MessageLength = 4
        msg.Info = value
        self.conn.send(msg)

    def reply_throughput(self, stats):
        msg = Msg1()
        msg.MessageType = 3 # Reply
        msg.MessageLength = 36 # throughput stats + type
        msg.Info = 1 # REPLY_STATS_THROUGHPUT
        reply = ThroughputReply()
        reply.Duration = int(1e6*stats.duration)
        reply.RxBytes = stats.RxBytes
        reply.TxBytes = stats.TxBytes
        reply.ReqCount = stats.RxReqs
        reply.CorrectIAD = stats.ia_is_correct
        replyBuf = io.BytesIO()
        replyBuf.write(msg)
        replyBuf.write(reply)
        self.conn.send(replyBuf.getvalue())
        replyBuf.close()

    def reply_latency(self, stats):
        msg = Msg1()
        msg.MessageType = 3 # Reply
        msg.MessageLength = 140 # latency stats + type
        msg.Info = 2 # REPLY_STATS_LATENCY
        reply = LatencyReply()
        reply.Th_data.Duration = int(1e6*stats.duration)
        reply.Th_data.RxBytes = stats.throughput_stats.RxBytes
        reply.Th_data.TxBytes = stats.throughput_stats.TxBytes
        reply.Th_data.ReqCount = stats.throughput_stats.RxReqs
        reply.Th_data.CorrectIAD = stats.throughput_stats.ia_is_correct
        reply.Avg_latency = stats.Avg_latency
        reply.P50i = stats.P50i
        reply.P50 = stats.P50
        reply.P50k = stats.P50k
        reply.P90i = stats.P90i
        reply.P90 = stats.P90
        reply.P90k = stats.P90k
        reply.P95i = stats.P95i
        reply.P95 = stats.P95
        reply.P95k = stats.P95k
        reply.P99i = stats.P99i
        reply.P99 = stats.P99
        reply.P99k = stats.P99k
        reply.P999i = stats.P999i
        reply.P999 = stats.P999
        reply.P999k = stats.P999k
        reply.P9999i = stats.P9999i
        reply.P9999 = stats.P9999
        reply.P9999k = stats.P9999k
        reply.P99999i = stats.P99999i
        reply.P99999 = stats.P99999
        reply.P99999k = stats.P99999k
        reply.P999999i = stats.P999999i
        reply.P999999 = stats.P999999
        reply.P999999k = stats.P999999k
        reply.IsIID = stats.IsIID
        reply.ToReduceSampling = stats.ToReduce
        reply.IsStationary = stats.is_stationary
        replyBuf = io.BytesIO()
        replyBuf.write(msg)
        replyBuf.write(reply)
        self.conn.send(replyBuf.getvalue())
        replyBuf.close()

    def close(self):
        self.conn.close()
