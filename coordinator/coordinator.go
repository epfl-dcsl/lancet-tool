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
	"fmt"
	"math"
	"strconv"
	"strings"
	"time"
)

type coordState int
type coordinator struct {
	thAgents     []*agent
	ltAgents     []*agent
	symAgents    []*agent
	agentPort    int
	samples      int
	ciSize       int
	state        coordState
	samplingRate float64
}

const (
	initialSamplingRate            = 20
	initialSamples                 = 10000
	maxTries                       = 10
	maxDuration                    = 900 // 15 minutes
	Load                coordState = 0
	Measure             coordState = 1
	Exit                coordState = 2
	samplesStep                    = 10000
)

func (c *coordinator) load(loadRate, latencyRate int) error {
	ok := false

	for i := 0; i < 60; i++ {
		ret, err := check_conn_open(append(c.symAgents, c.thAgents...))
		if err != nil {
			return fmt.Errorf("Error waiting for connections: %v\n", err)
		}
		if ret {
			ok = true
			break
		}
		time.Sleep(1 * time.Second)
	}

	if !ok {
		return fmt.Errorf("Connections didn't open within time limit\n")
	}

	// Start loading
	if len(c.symAgents) > 0 || len(c.thAgents) > 0 {
		perAgentLoad := int(loadRate / (len(c.symAgents) + len(c.thAgents)))
		err := startLoad(append(c.symAgents, c.thAgents...), perAgentLoad)
		if err != nil {
			return fmt.Errorf("Error setting load: %v\n", err)
		}
	}
	// Start latency agents too
	if len(c.ltAgents) > 0 {
		err := startLoad(c.ltAgents, latencyRate)
		if err != nil {
			return fmt.Errorf("Error setting load: %v\n", err)
		}
	}
	return nil
}

func (c *coordinator) fixedPattern(loadRate, latencyRate int) error {
	fmt.Printf("Load rate is %v\n", loadRate)
	fmt.Printf("Latency rate = %v\n", latencyRate)
	if len(c.symAgents) == 0 && len(c.thAgents) == 0 && len(c.ltAgents) == 0 {
		return fmt.Errorf("There are no agents")
	}

	err := c.load(loadRate, latencyRate)
	if err != nil {
		return err
	}
	time.Sleep(time.Duration(2) * time.Second)

	var perAgentLoad int
	var baseRate float64
	var samplingRate float64
	if len(c.symAgents) > 0 || len(c.thAgents) > 0 {
		perAgentLoad = int(loadRate / (len(c.symAgents) + len(c.thAgents)))
	} else {
		perAgentLoad = 0
	}
	if len(c.ltAgents) > 0 {
		baseRate = float64(latencyRate)
	} else {
		baseRate = float64(perAgentLoad)
	}
	if len(c.symAgents) > 0 || len(c.ltAgents) > 0 {
		samplingRate = c.samplingRate * float64(len(c.symAgents)+len(c.ltAgents))
	} else {
		samplingRate = 100
	}
	duration := int(math.Ceil(float64(c.samples) / (baseRate * (samplingRate / 100.0))))
	fmt.Printf("Will run for %v sec\n", duration)

	err = startMeasure(append(append(c.thAgents, c.ltAgents...), c.symAgents...), c.samples, samplingRate)
	if err != nil {
		return fmt.Errorf("Error starting measuring: %v\n", err)
	}
	time.Sleep(time.Duration(duration) * time.Second)

	var throughputReplies []*C.struct_throughput_reply
	var latencyReplies []*C.struct_latency_reply
	var e error

	if len(c.thAgents) > 0 {
		throughputReplies, e = reportThroughput(c.thAgents)
		if e != nil {
			return fmt.Errorf("Error getting throughput replies: %v\n", e)
		}
	} else {
		throughputReplies = make([]*C.struct_throughput_reply, 0)
	}

	if len(c.ltAgents) > 0 || len(c.symAgents) > 0 {
		latencyReplies, e = reportLatency(append(c.ltAgents, c.symAgents...))
		if e != nil {
			return fmt.Errorf("Error getting latency replies: %v\n", e)
		}

		for _, reply := range latencyReplies {
			latAgentThroughput := &reply.Th_data
			throughputReplies = append(throughputReplies, latAgentThroughput)
		}
	}

	agg_throughput := computeStatsThroughput(throughputReplies)
	printThroughputStats(agg_throughput)

	if len(c.ltAgents) > 0 || len(c.symAgents) > 0 {
		aggLatency := computeStatsLatency(latencyReplies)
		fmt.Println("Aggregate latency")
		printLatencyStats(aggLatency)
	}
	return nil
}

func (c *coordinator) fixedQualPattern(loadRate, latencyRate int) error {
	var err error
	tryCount := 0
	expectedRPS := float64(loadRate) + float64(latencyRate)
	c.state = Load
	maxTimeReached := false
	for tryCount < maxTries {
		switch c.state {
		case Load:
			err = c.load(loadRate, latencyRate)
			if err != nil {
				return err
			}
			// Wait
			time.Sleep(1 * time.Second)
			// Measure
			samplingRate := c.samplingRate * float64(len(c.symAgents)+len(c.ltAgents))
			err = startMeasure(append(append(c.thAgents, c.ltAgents...), c.symAgents...), c.samples, samplingRate)
			if err != nil {
				return fmt.Errorf("Error starting measuring: %v\n", err)
			}
			time.Sleep(1 * time.Second)
			// Collect throughput
			throughputReplies, e := reportThroughput(append(append(c.thAgents, c.ltAgents...), c.symAgents...))
			if e != nil {
				return fmt.Errorf("Error getting throughput replies: %v\n", e)
			}
			aggThroughput := computeStatsThroughput(throughputReplies)
			rps := getRPS(aggThroughput)
			// Check if throughput reached
			if rps > expectedRPS*1.1 || rps < expectedRPS*0.90 {
				tryCount += 1
				fmt.Println("Throughput is wrong")
				fmt.Printf("Throughput should be between %v %v and is %v\n", 0.9*expectedRPS, 1.1*expectedRPS, rps)
				continue
			}

			// Check if inter-arrival is right
			if aggThroughput.CorrectIAD == 0 {
				tryCount += 1
				fmt.Println("Inter-arrival is wrong")
				continue
			}
			c.state = Measure
		case Measure:
			// Start measuring
			samplingRate := c.samplingRate * float64(len(c.symAgents)+len(c.ltAgents))
			err = startMeasure(append(append(c.thAgents, c.ltAgents...), c.symAgents...), c.samples, samplingRate)
			if err != nil {
				return fmt.Errorf("Error starting measuring: %v\n", err)
			}

			// Wait to collec the necessary samples
			var baseRate float64
			if len(c.ltAgents) > 0 {
				baseRate = float64(latencyRate)
			} else {
				baseRate = float64(loadRate / (len(c.symAgents) + len(c.thAgents)))
			}
			duration := int(math.Ceil(float64(c.samples) / (baseRate * (samplingRate / 100.0))))
			fmt.Printf("Will run for %v seconds\n", duration)
			if duration > maxDuration {
				maxTimeReached = true
			}
			time.Sleep(time.Duration(duration) * time.Second)

			latencyReplies, e1 := reportLatency(append(c.symAgents, c.ltAgents...))
			if e1 != nil {
				return fmt.Errorf("Error getting latencyReplies replies: %v\n", e1)
			}
			agg_lat := computeStatsLatency(latencyReplies)

			if !maxTimeReached {
				// Check if stationary
				//if agg_lat.IsStationary == 0 {
				//	c.samples += samplesStep
				//	tryCount += 1
				//	continue
				//}
				fmt.Println("It's stationary")

				// Check if IID
				if agg_lat.IsIid == 0 {
					fmt.Printf("It's not IID.\n")
					if c.samplingRate < 0.01 {
						fmt.Println("Can't reduce sampling further")
					} else {
						fmt.Printf("Will reduce by %v\n", agg_lat.ToReduceSampling)
						c.samplingRate /= float64(agg_lat.ToReduceSampling)
						tryCount += 1
						continue
					}
				}
				fmt.Println("It's iid")

				// Check CI
				//if int(agg_lat.P99_k-agg_lat.P99_i) > (c.ciSize * 1000) {
				//	fmt.Printf("CI size = %v, target = %v\n",
				//		agg_lat.P99_k-agg_lat.P99_i, c.ciSize)
				//	c.samples += samplesStep
				//	tryCount += 1
				//	continue
				//}
				fmt.Println("CIs are ok")
			}

			fmt.Printf("Final sampling rate: %v\n", c.samplingRate)
			fmt.Printf("Final #samples: %v\n", c.samples)
			// Collect throughput
			var throughputReplies []*C.struct_throughput_reply

			if len(c.thAgents) > 0 {
				throughputReplies, err = reportThroughput(c.thAgents)
				if err != nil {
					return fmt.Errorf("Error getting throughput replies: %v\n", err)
				}
			} else {
				throughputReplies = make([]*C.struct_throughput_reply, 0)
			}

			for _, reply := range latencyReplies {
				latAgentThroughput := &reply.Th_data
				throughputReplies = append(throughputReplies, latAgentThroughput)
			}

			agg_throughput := computeStatsThroughput(throughputReplies)
			printThroughputStats(agg_throughput)
			fmt.Println("Aggregate latency")
			printLatencyStats(agg_lat)
			if maxTimeReached {
				return fmt.Errorf("Max time reached\n")
			}

			c.state = Exit
		case Exit:
			fmt.Println("Exit")
			time.Sleep(1 * time.Second)
			return nil
		}
	}
	return fmt.Errorf("Max re-tries or max time reached\n")
}

// Measure directly for a specific amount of time independent of target samples
func (c *coordinator) fixedTimePattern(loadRate, latencyRate, duration int) error {
	fmt.Printf("Load rate is %v\n", loadRate)
	fmt.Printf("Latency rate = %v\n", latencyRate)
	if len(c.symAgents) == 0 && len(c.thAgents) == 0 && len(c.ltAgents) == 0 {
		return fmt.Errorf("There are no agents")
	}

	err := c.load(loadRate, latencyRate)
	if err != nil {
		return err
	}

	fmt.Printf("Will run for %v sec\n", duration)

	err = startMeasure(append(append(c.thAgents, c.ltAgents...), c.symAgents...), c.samples, 100)
	if err != nil {
		return fmt.Errorf("Error starting measuring: %v\n", err)
	}
	time.Sleep(time.Duration(duration) * time.Second)

	var throughputReplies []*C.struct_throughput_reply
	var latencyReplies []*C.struct_latency_reply
	var e error

	if len(c.thAgents) > 0 {
		throughputReplies, e = reportThroughput(c.thAgents)
		if e != nil {
			return fmt.Errorf("Error getting throughput replies: %v\n", e)
		}

	} else {
		throughputReplies = make([]*C.struct_throughput_reply, 0)
	}

	if len(c.ltAgents) > 0 || len(c.symAgents) > 0 {
		latencyReplies, e = reportLatency(append(c.ltAgents, c.symAgents...))
		if e != nil {
			return fmt.Errorf("Error getting latency replies: %v\n", e)
		}

		for _, reply := range latencyReplies {
			latAgentThroughput := &reply.Th_data
			throughputReplies = append(throughputReplies, latAgentThroughput)
		}
	}

	agg_throughput := computeStatsThroughput(throughputReplies)
	printThroughputStats(agg_throughput)

	if len(c.ltAgents) > 0 || len(c.symAgents) > 0 {
		aggLatency := computeStatsLatency(latencyReplies)
		fmt.Println("Aggregate latency")
		printLatencyStats(aggLatency)
	}
	return nil
}

func (c *coordinator) stepPattern(startLoad, endLoad, step, latencyRate int, pattern string) error {
	fmt.Printf("%v %v %v\n", startLoad, endLoad, step)
	loadRate := startLoad
	var err error
	for loadRate <= endLoad {
		if pattern == "step" {
			fmt.Println("call fixed pattern")
			err = c.fixedPattern(loadRate, latencyRate)
		} else {
			err = c.fixedQualPattern(loadRate, latencyRate)
		}
		if err != nil {
			fmt.Println(err)
			//return err
		}
		fmt.Println()
		fmt.Println("Increase load")
		loadRate += step
	}
	return nil
}

// Repeat measuring for 1 second
func (c *coordinator) fixedRepeatPattern(loadRate, repetitions int) error {
	var err error
	for i := 0; i < repetitions; i++ {
		err = c.fixedTimePattern(loadRate, 0, 1)
		if err != nil {
			fmt.Println(err)
		}
	}
	return nil
}

func (c *coordinator) runExp(pattern string, latencyRate, ciSize int) error {

	patternArgs := strings.Split(pattern, ":")
	c.samples = initialSamples
	c.samplingRate = initialSamplingRate
	c.ciSize = ciSize

	if len(c.ltAgents) == 0 {
		latencyRate = 0
	}
	if patternArgs[0] == "fixed" || patternArgs[0] == "fixedQual" || patternArgs[0] == "fixedSteady" {
		loadRate, err := strconv.Atoi(patternArgs[1])
		if err != nil {
			return fmt.Errorf("Error parsing load\n")
		}
		if len(patternArgs) > 2 {
			samples, err := strconv.Atoi(patternArgs[2])
			if err != nil {
				return fmt.Errorf("Error parsing samples\n")
			}
			c.samples = samples
		}
		if len(patternArgs) > 3 {
			samplingRate, err := strconv.ParseFloat(patternArgs[3], 64)
			if err != nil {
				return fmt.Errorf("Error parsing sampling rate\n")
			}
			c.samplingRate = samplingRate
		}
		if patternArgs[0] == "fixed" {
			return c.fixedPattern(loadRate, latencyRate)
		} else if patternArgs[0] == "fixedRepeat" {
			return c.fixedRepeatPattern(loadRate, 100)
		} else {
			return c.fixedQualPattern(loadRate, latencyRate)
		}
	} else if patternArgs[0] == "step" || patternArgs[0] == "stepQual" {
		startLoad, err := strconv.Atoi(patternArgs[1])
		if err != nil {
			return fmt.Errorf("Error parsing start load\n")
		}
		endLoad, err := strconv.Atoi(patternArgs[2])
		if err != nil {
			return fmt.Errorf("Error parsing end load\n")
		}
		step, err := strconv.Atoi(patternArgs[3])
		if err != nil {
			return fmt.Errorf("Error parsing step load\n")
		}
		if len(patternArgs) > 4 {
			samples, err := strconv.Atoi(patternArgs[4])
			if err != nil {
				return fmt.Errorf("Error parsing samples\n")
			}
			c.samples = samples
		}
		if len(patternArgs) > 5 {
			samplingRate, err := strconv.ParseFloat(patternArgs[5], 64)
			if err != nil {
				return fmt.Errorf("Error parsing sampling rate\n")
			}
			c.samplingRate = samplingRate
		}
		return c.stepPattern(startLoad, endLoad, step, latencyRate, patternArgs[0])
	} else {
		return fmt.Errorf("Unknown load pattern")
	}
	return nil
}
