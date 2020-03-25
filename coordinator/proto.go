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
package main

// #include "../inc/lancet/coord_proto.h"
import "C"
import (
	"bytes"
	"encoding/binary"
	"fmt"
	"time"
	/*
		"strings"
	*/)

func broadcastMessage(msg *bytes.Buffer, agents []*agent) error {
	for _, a := range agents {
		count, err := a.conn.Write(msg.Bytes())
		if err != nil {
			return fmt.Errorf("Write to server failed: %v\n", err)
		}
		if count != msg.Len() {
			panic("Conn write mismatch")
		}
	}
	return nil
}

func collectAcks(agents []*agent) error {
	// Wait for ACK with a 2 second deadline
	timeOut := 500 * time.Millisecond
	for _, a := range agents {
		a.conn.SetReadDeadline(time.Now().Add(timeOut))
		reply := &C.struct_msg1{}
		data := make([]byte, 64)
		_, err := a.conn.Read(data)
		if err != nil {
			return fmt.Errorf("Read from agent failed: %v\n", err)
		}
		r := bytes.NewReader(data)
		err = binary.Read(r, binary.LittleEndian, reply)
		if err != nil {
			return fmt.Errorf("Error parsing ACK: %v\n", err)
		}
		if reply.Info != C.REPLY_ACK {
			return fmt.Errorf("Didn't receive ACK.\n")
		}
	}
	return nil
}

func collectValues(agents []*agent) ([]int, error) {
	ret := make([]int, len(agents))

	// Wait for value with a 2 second deadline
	timeOut := 500 * time.Millisecond
	for i, a := range agents {
		a.conn.SetReadDeadline(time.Now().Add(timeOut))
		reply := &C.struct_msg1{}
		data := make([]byte, 64)
		_, err := a.conn.Read(data)
		if err != nil {
			return nil, fmt.Errorf("Read from agent failed: %v\n", err)
		}
		r := bytes.NewReader(data)
		err = binary.Read(r, binary.LittleEndian, reply)
		if err != nil {
			return nil, fmt.Errorf("Error parsing value: %v\n", err)
		}
		ret[i] = int(reply.Info)
	}
	return ret, nil
}

func collectThroughputResults(agents []*agent) ([]*C.struct_throughput_reply, error) {
	result := make([]*C.struct_throughput_reply, 0)
	timeOut := 5000 * time.Millisecond
	for _, a := range agents {
		a.conn.SetReadDeadline(time.Now().Add(timeOut))
		reply := &C.struct_throughput_reply{}
		prelude := &C.struct_msg1{}
		data := make([]byte, 1024)
		_, err := a.conn.Read(data)
		if err != nil {
			return nil, fmt.Errorf("Read from agent failed: %v\n", err)
		}
		r := bytes.NewReader(data)

		// Read throughput reply
		err = binary.Read(r, binary.LittleEndian, prelude)
		if err != nil {
			return nil, fmt.Errorf("Error parsing throughput_reply header: %v\n", err)
		}
		if prelude.Info != C.REPLY_STATS_THROUGHPUT {
			return nil, fmt.Errorf("Didn't receive throughput stats\n")
		}
		err = binary.Read(r, binary.LittleEndian, reply)
		if err != nil {
			return nil, fmt.Errorf("Error parsing throughput_reply: %v\n", err)
		}
		result = append(result, reply)
	}
	return result, nil
}

func collectLatencyResults(agents []*agent) ([]*C.struct_latency_reply, error) {
	result := make([]*C.struct_latency_reply, 0)
	timeOut := 10000 * time.Millisecond
	aggregate := 0
	for _, a := range agents {
		a.conn.SetReadDeadline(time.Now().Add(timeOut))
		reply := &C.struct_latency_reply{}
		prelude := &C.struct_msg1{}
		data := make([]byte, 1024)
		_, err := a.conn.Read(data)

		if err != nil {
			return nil, fmt.Errorf("Read from agent failed: %v\n", err)
		}
		r := bytes.NewReader(data)
		err = binary.Read(r, binary.LittleEndian, prelude)
		if err != nil {
			return nil, fmt.Errorf("Error parsing latency_reply header: %v\n", err)
		}
		if prelude.Info != C.REPLY_STATS_LATENCY {
			return nil, fmt.Errorf("Didn't receive latency stats\n")
		}
		err = binary.Read(r, binary.LittleEndian, reply)
		if err != nil {
			return nil, fmt.Errorf("Error parsing latency_reply: %v\n", err)
		}
		aggregate += int(reply.Th_data.CorrectIAD)
		result = append(result, reply)
	}
	fmt.Printf("Overall IA check: %v\n", aggregate)
	return result, nil
}

func collectConvergenceResults(agents []*agent) ([]int, error) {
	// Wait for ACK with a 2 second deadline
	timeOut := 500 * time.Millisecond
	res := make([]int, len(agents))
	for i, a := range agents {
		a.conn.SetReadDeadline(time.Now().Add(timeOut))
		prelude := &C.struct_msg1{}
		var conv uint32
		data := make([]byte, 64)
		_, err := a.conn.Read(data)
		if err != nil {
			return nil, fmt.Errorf("Read from agent failed: %v\n", err)
		}
		r := bytes.NewReader(data)
		err = binary.Read(r, binary.LittleEndian, prelude)
		if err != nil {
			return nil, fmt.Errorf("Error parsing convergence: %v\n", err)
		}
		if prelude.Info != C.REPLY_CONVERGENCE {
			return nil, fmt.Errorf("Didn't receive convergence\n")
		}
		err = binary.Read(r, binary.LittleEndian, &conv)
		if err != nil {
			return nil, fmt.Errorf("Error parsing convergence: %v\n", err)
		}
		res[i] = int(conv)
	}
	return res, nil
}

// Messages sent from the coordinator to the agents
func startLoad(agents []*agent, load int) error {
	msg := C.struct_msg1{
		Hdr: C.struct_msg_hdr{
			MessageType:   C.uint32_t(C.START_LOAD),
			MessageLength: C.uint32_t(4),
		},
		Info: C.uint32_t(load),
	}
	buf := &bytes.Buffer{}
	err := binary.Write(buf, binary.LittleEndian, msg)
	if err != nil {
		return fmt.Errorf("Error formating message: %v", err)
	}
	err = broadcastMessage(buf, agents)
	if err != nil {
		return err
	}
	err = collectAcks(agents)
	if err != nil {
		return err
	}

	return nil
}

func startMeasure(agents []*agent, sampleCount int, samplingRate float64) error {
	Hdr := C.struct_msg_hdr{
		MessageType:   C.uint32_t(C.START_MEASURE),
		MessageLength: C.uint32_t(12),
	}
	Info1 := C.uint32_t(sampleCount)
	Info2 := C.double(samplingRate)
	buf := &bytes.Buffer{}
	err := binary.Write(buf, binary.LittleEndian, Hdr)
	if err != nil {
		return fmt.Errorf("Error formating message: %v", err)
	}
	err = binary.Write(buf, binary.LittleEndian, Info1)
	if err != nil {
		return fmt.Errorf("Error formating message: %v", err)
	}
	err = binary.Write(buf, binary.LittleEndian, Info2)
	if err != nil {
		return fmt.Errorf("Error formating message: %v", err)
	}
	err = broadcastMessage(buf, agents)
	if err != nil {
		return err
	}
	err = collectAcks(agents)
	if err != nil {
		return err
	}
	return nil
}

func reportThroughput(agents []*agent) ([]*C.struct_throughput_reply, error) {
	msg := C.struct_msg1{
		Hdr: C.struct_msg_hdr{
			MessageType:   C.uint32_t(C.REPORT_REQ),
			MessageLength: C.uint32_t(4),
		},
		Info: C.uint32_t(C.REPORT_THROUGHPUT),
	}
	buf := &bytes.Buffer{}
	err := binary.Write(buf, binary.LittleEndian, msg)
	if err != nil {
		return nil, fmt.Errorf("Error formating message: %v", err)
	}
	err = broadcastMessage(buf, agents)
	if err != nil {
		return nil, err
	}
	return collectThroughputResults(agents)
}

func reportLatency(agents []*agent) ([]*C.struct_latency_reply, error) {
	msg := C.struct_msg1{
		Hdr: C.struct_msg_hdr{
			MessageType:   C.uint32_t(C.REPORT_REQ),
			MessageLength: C.uint32_t(4),
		},
		Info: C.uint32_t(C.REPORT_LATENCY),
	}
	buf := &bytes.Buffer{}
	err := binary.Write(buf, binary.LittleEndian, msg)
	if err != nil {
		return nil, fmt.Errorf("Error formating message: %v", err)
	}
	err = broadcastMessage(buf, agents)
	if err != nil {
		return nil, err
	}
	return collectLatencyResults(agents)
}

func check_conn_open(agents []*agent) (bool, error) {
	msg := C.struct_msg1{
		Hdr: C.struct_msg_hdr{
			MessageType:   C.uint32_t(C.CONN_OPEN),
			MessageLength: C.uint32_t(4),
		},
		Info: C.uint32_t(0),
	}
	buf := &bytes.Buffer{}
	err := binary.Write(buf, binary.LittleEndian, msg)
	if err != nil {
		return false, fmt.Errorf("Error formating message: %v", err)
	}
	err = broadcastMessage(buf, agents)
	if err != nil {
		return false, err
	}
	conn_open, err := collectValues(agents)
	if err != nil {
		return false, err
	}
	for _, c := range conn_open {
		if c == 0 {
			return false, nil
		}
	}
	return true, nil
}
