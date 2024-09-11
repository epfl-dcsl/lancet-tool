# Lancet
Lancet is a distributed latency measuring tool. It leverages NIC-based timestamping to improve measuring accuracy and it depends on a self-correcting measuring methodology to eliminate measuring pitfalls.
For more refer to the Lancet [paper](https://www.usenix.org/conference/atc19/presentation/kogias-lancet).

# Building Lancet

Lancet has multiple components:

* The coordinator that launches and manages all agents is a self-contained Golang program found in `coordinator/`. It is built using standard `go build`
* The agents that perform throughput and latency are written in C and are found in the `agents/` directory. They are built using `cmake` (and in turn `ninja` or `make`, depending on what you have installed).
* The agent manager is a pure Python program that is built into a package (wheel in Python parlance) and is in `agent-manager`.

You need to build the different components separately.
Making the agents manually involves using CMake.
You may need a newer version of CMake than your distribution provides.
We recommend installing CMake via `pip`:

```
pip3 install cmake
```
## Vanilla Lancet build
Lancet supports TCP and UDP with hardware timestamping. To build run the following:
```
make coordinator
make agents
make manager
```

## Building with R2P2
If you also want to build with [R2P2](https://github.com/epfl-dcsl/r2p2) run the following:
```
make coordinator
make agents_r2p2 R2P2=<path_to_r2p2_folder_in_the r2p2_repository>
make manager
```

## Building with R2P2 with hardware timestamping support
If you want to use the hardware timestamping support for R2P2 run the following:
```
make coordinator
make agents_r2p2_nic_ts R2P2=<path_to_r2p2_folder_in_the r2p2_repository>
make manager
```
Note: For TCP and UDP hardware timestamping you don't need to build differently

# Running Lancet
Lancet is a distributed tool. There are several agents and one coordinator. The coordinator is in charge of spawning and controlling the agents. So, users are expected first deploy the lancet agents and then only interact with them through the coordinator.

**Note:** In order to use Lancet's hardware timestamping feature you will need a Linux kernel >= 4.19.4. Prior kernel versions might lead to incorrect results. Also, you need a NIC with hardware timestamping support. We've tested Lancet with Mellanox Connect-x4.

## Deploy Lancet Agents
Before running Lancet you need to deploy the agents.
```
make deploy HOSTS=<comma_separated_list_of_hosts>
```
The above command will copy the necessary Lancet assets and install them in ``/tmp/<username>/lancet``. Lancet uses a python virtualenv. So, it assumes there is a ``virtualenv`` is installed in the agent machines.

## Arguments
Lancet supports a series of arguments, most of them are self explanatory.
```
$./coordinator/coordinator -h
Usage of ./coordinator/coordinator:
  -agentPort int
    	listening port of the agent (default 5001)
  -appProto string
    	application protocol (default "echo:4")
  -ciSize int
    	size of 95-confidence interval in us (default 10)
  -comProto string
    	TCP|R2P2|UDP (default "TCP")
  -idist string
    	interarrival distibution: fixed, exp (default "exp")
  -ifName string
    	interface name for hardware timestamping (default "enp65s0")
  -loadAgents string
    	ip of loading agents separated by commas, e.g. ip1,ip2,... (this can be specified along with symAgents.  These would add additional load)
  -loadConns int
    	number of loading connections per agent (used for both load and sym agents) (default 1)
  -loadPattern string
    	load pattern (default "fixed:10000")
  -loadThreads int
    	loading threads per agent (used for load and sym agents) (default 1)
  -lqps int
    	latency qps (default 4000)
  -ltAgents string
    	ip of latency agents separated by commas, e.g. ip1,ip2,...
  -ltConns int
    	number of latency connections (default 1)
  -ltThreads int
    	latency threads per agent (default 1)
  -nicTS
    	NIC timestamping for symmetric agents
  -privateKey string
    	location of the (local) private key to deploy the agents. Will find a default if not specified (default "$HOME/.ssh/id_rsa")
  -reqPerConn int
    	Number of outstanding requests per TCP connection (used for both load and sym agents) (default 1)
  -symAgents string
    	ip of latency agents separated by commas, e.g. ip1,ip2,...
  -targetHost string
    	host:port comma-separated list to run experiment against (default "127.0.0.1:8000")
```

## Application Protocols
Lancet supports a few application protocols, while it can be easily extended with new ones. Currently, we support the following protocols:

### Echo protocol
``-appProto echo:<number_of_bytes>``<br/>
This protocol sends the specified number of bytes and waits the same number back as an answer. For example, ``echo:4`` will send and expect 4 bytes as a response.

### Synthetic protocol
``-appProto synthetic:<random_generator>``<br/>
This protocol sends a random long integer (8 bytes) as a payload and expects an long integer as a reply, too. This protocol is used in synthetic service time microbenchmarks. For example, ``synthetic:fixed:10`` always sends the number 10 as a payload, while ``synthetic:exp:10`` generates and sends random numbers from an exponential distribution with an average of 10.

### KV-store Protocols
Currently lancet supports 3 different key-value store protocols: **binary memcached**, **ascii memcached**, and **Redis**. Defining the KV-store workload is the same across all protocols.
```
-appProto <memcache-bin|memcache-ascii|redis>_<key_size_random_generator>_<value_size_random_generator>_<key_count>_<read_write_ratio>_<key_selector(rr|uni)>
```

For example ``memcache-bin_fixed:10_fixed:2_1000000_0.998_uni`` specifies a KV-store workload with 1000000 keys of with fixed size of 10 bytes, fixed values of 2 bytes, 0.2 % writes, and random uniform key access pattern (as opposed to round robin).

### HTTP Protocol

Running with the HTTP agent requires the following parameters to be passed to the `coordinator`:

* `-comProto TCP`: http only works over TCP
* `-targetHost HOSTNAME:80`, where `HOSTNAME` is the name of the server (and presumably serving HTTP over the standard port 80)
* `-appProto http:SITE/path/to/asset.html`: simply put, the app proto is `http:` followed by a URL. The parameters to make a valid HTTP request are placed in the `appProto` argument. `SITE` is the name of the site (e.g. `example.com`) that is placed in the `Host:` field of the HTTP request. `/path/to/asset.html` is the path to the asset requested, e.g. just `/index.html`; this is used as parameters to the `GET` method in the HTTP request.

#### Troubleshooting

Lancet uses long-running connections that opens at the beginning of the experiment.
If you receive an error message saying `"Connection closed"` when running Lancet against your webserver, keep in mind that some webservers limit the number of total requests allowed on a single connection.
For example, in `nginx` you will have to [adjust this number to something _much_ larger than 100](https://nginx.org/en/docs/http/ngx_http_core_module.html#keepalive_requests).

## Random generators
Lancet implements a series of random generators that are used for the workload configuration, e.g. the inter-arrival time and application protocols. The most commonly used are the following:

1. Fixed ``fixed:<val>``
2. Exponential ``exp:<avg>``
3. Bimodal ``bimodal:<val1>:<val2>:<percentage of val1>``
4. Uniform Random ``uni:<upper_limit>`` positive integers up to the upper limit
5. Round Robin ``rr:<upper_limit>``

For more check the ``init_rand`` function in ``agents/rand_gen.c``.

## Load Patterns
Lancet supports the following load patterns:

1. ``fixed:<QPS>[:<#Samples>[:<Sampling_Rate>]]``<br/>
Fixed load level **without** the self-correcting methodology.  Sampling_Rate is specified as a percentage.  So 5 means 5%.

2. ``fixedQual:<QPS>[:<#Samples>[:<Sampling_Rate>]]``<br/>
Fixed load level **with** the self-correcting methodology. The extra arguments only configure the initial values for #Samples and sampling rate.

3. ``step:<StartQPS>:<EndQPS>:<STEP>[:<#Samples>[:<Sampling_Rate>]]``<br/>
Step load pattern **without** the self-correcting methodology. The extra arguments only configure the initial values for #Samples and sampling rate.

4. ``stepQual:<StartQPS>:<EndQPS>:<STEP>[:<#Samples>[:<Sampling_Rate>]]``<br/>
Step load pattern **with** the self-correcting methodology used in every step.

Note: Arguments in parentesis are optional.
The default #Samples is 10000.  Default Sampling_Rate is %20.
When running without self-correcting methodology, the test stops after the required number of <#Samples> is collected.  The non self-correcting test also doesn't ensure that actual throughput equals expected throughput.  For example, actual throughput might be much lower than expected throughput because the throughput-agent cannot generate the required load.
For example, if ltAgents is used and lqps is 4000, #Samples is 10000, and sampling rate is %20.  Then it would take roughly 10000/(4000*0.20) seconds = 12.5 seconds to finish the test.
