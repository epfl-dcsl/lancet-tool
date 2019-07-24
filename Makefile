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

.PHONY: coordinator agents style manager manager_r2p2 all clean

.DEFAULT_GOAL := all
all: agents
	${MAKE} manager
	${MAKE} coordinator

current_dir= $(shell pwd)

manager:
	${MAKE} -C ${current_dir}/agent-manager

coordinator:
	${MAKE} -C coordinator/

agents: clean
	mkdir agents/assets
	cmake -DCMAKE_BUILD_TYPE=Release -S . -B agents/assets
	cmake	--build agents/assets

agents_r2p2: clean
	mkdir agents/assets
	cmake -DBUILD_R2P2=ON -DR2P2_ROOT=${R2P2} -S . -B agents/assets
	cmake	--build agents/assets

agents_r2p2_nic_ts: clean
	mkdir agents/assets
	cmake -DBUILD_R2P2=ON -DR2P2_ROOT=${R2P2} -DR2P2_NIC_TS=ON -S . -B agents/assets
	cmake	--build agents/assets

deploy: clean_remote
	scripts/deploy_lancet.sh ${HOSTS}

prepare_clients:
	scripts/prepare_clients.sh ${HOSTS}

clean_remote:
	scripts/clean_up.sh ${HOSTS}

clean:
	rm -rf agents/assets

style:
	clang-format -i -style=file agents/*.c
	clang-format -i -style=file inc/lancet/*.h
