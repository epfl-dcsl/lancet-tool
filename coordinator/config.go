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

import (
	"flag"
	"fmt"
	"os"
	"os/user"
	"path"
	"strings"
)

type ServerConfig struct {
	target     string
	thThreads  int
	ltThreads  int
	thConn     int
	ltConn     int
	idist      string
	appProto   string
	comProto   string
	ifName     string
	reqPerConn int
}

type ExperimentConfig struct {
	thAgents       []string
	ltAgents       []string
	symAgents      []string
	agentPort      int
	ltRate         int
	loadPattern    string
	ciSize         int
	nicTS          bool
	privateKeyPath string
}

type GeneralConfig struct {
	runAgents      bool
	printAgentArgs bool
}

func ParseConfig() (*ServerConfig, *ExperimentConfig, *GeneralConfig, error) {
	currentUser, _ := user.Current()
	id_rsa_path := path.Join(currentUser.HomeDir, ".ssh/id_rsa")
	var agentPort = flag.Int("agentPort", 5001, "listening port of the agent")
	var target = flag.String("targetHost", "127.0.0.1:8000", "host:port comma-separated list to run experiment against")
	var thAgents = flag.String("loadAgents", "", "ip of loading agents separated by commas, e.g. ip1,ip2,...")
	var ltAgents = flag.String("ltAgents", "", "ip of latency agents separated by commas, e.g. ip1,ip2,...")
	var symAgents = flag.String("symAgents", "", "ip of latency agents separated by commas, e.g. ip1,ip2,...")
	var thThreads = flag.Int("loadThreads", 1, "loading threads per agent (used for load and sym agents)")
	var ltThreads = flag.Int("ltThreads", 1, "latency threads per agent")
	var thConn = flag.Int("loadConns", 1, "number of loading connections per agent")
	var ltConn = flag.Int("ltConns", 1, "number of latency connections")
	var idist = flag.String("idist", "exp", "interarrival distibution: fixed, exp")
	var appProto = flag.String("appProto", "echo:4", "application protocol")
	var comProto = flag.String("comProto", "TCP", "TCP|R2P2|UDP")
	var ltRate = flag.Int("lqps", 4000, "latency qps")
	var loadPattern = flag.String("loadPattern", "fixed:10000", "load pattern")
	var ciSize = flag.Int("ciSize", 10, "size of 95-confidence interval in us")
	var nicTS = flag.Bool("nicTS", false, "NIC timestamping for symmetric agents")
	var privateKey = flag.String("privateKey", id_rsa_path, "location of the (local) private key to deploy the agents. Will find a default if not specified")
	var ifName = flag.String("ifName", "enp65s0", "interface name for hardware timestamping")
	var reqPerConn = flag.Int("reqPerConn", 1, "Number of outstanding requests per TCP connection")
	var runAgents = flag.Bool("runAgents", true, "Automatically run agents")
	var printAgentArgs = flag.Bool("printAgentArgs", false, "Print in JSON format the arguments for each agent")

	flag.Parse()

	serverCfg := &ServerConfig{}
	expCfg := &ExperimentConfig{}
	generalCfg := &GeneralConfig{}

	if *runAgents {
		if x, err := os.Stat(*privateKey); err != nil {
			fmt.Printf("Unable to find private ssh key at path '%s'\n", *privateKey)
			return nil, nil, nil, err
		} else if x.IsDir() {
			return nil, nil, nil, fmt.Errorf("Have a directory at ssh path %s\n", *privateKey)
		}
	}

	serverCfg.target = *target
	serverCfg.thThreads = *thThreads
	serverCfg.ltThreads = *ltThreads
	serverCfg.thConn = *thConn
	serverCfg.ltConn = *ltConn
	serverCfg.idist = *idist
	serverCfg.appProto = *appProto
	serverCfg.comProto = *comProto
	serverCfg.ifName = *ifName
	serverCfg.reqPerConn = *reqPerConn

	if *thAgents == "" {
		expCfg.thAgents = nil
	} else {
		expCfg.thAgents = strings.Split(*thAgents, ",")
	}
	if *ltAgents == "" {
		expCfg.ltAgents = nil
	} else {
		expCfg.ltAgents = strings.Split(*ltAgents, ",")
	}
	if *symAgents == "" {
		expCfg.symAgents = nil
	} else {
		expCfg.symAgents = strings.Split(*symAgents, ",")
	}
	expCfg.agentPort = *agentPort
	expCfg.ltRate = *ltRate
	expCfg.loadPattern = *loadPattern
	expCfg.ciSize = *ciSize
	expCfg.nicTS = *nicTS
	expCfg.privateKeyPath = *privateKey

	generalCfg.runAgents = *runAgents
	generalCfg.printAgentArgs = *printAgentArgs

	return serverCfg, expCfg, generalCfg, nil
}
