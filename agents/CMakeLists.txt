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
cmake_minimum_required( VERSION 3.12...3.13 )

project( Agent
  VERSION 1.0.0
  DESCRIPTION "Agent for Lancet")

set(CMAKE_C_FLAGS_RELEASEASSERT "-O3")

# first, lets find the flags we need
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
find_library(LIBRT rt REQUIRED)
find_library(LIBM m REQUIRED)

find_library(LIBCONFIG config)
if(NOT LIBM)
  message(FATAL_ERROR "libconfig not found")
endif()

# Setting this variable because both librand.so and agent link them in
set(RAND_SRCS "rand_gen.c" "cpp_rand.cc")
set(COMMON_CFLAGS "-Wall" )

# This is used by the Controller to do fast math
# SHARED means that PIC is on automatically
add_library( rand SHARED )
target_sources( rand
  PRIVATE ${RAND_SRCS} )
target_link_libraries( rand PRIVATE lancet )
target_compile_features( rand PRIVATE cxx_std_11 )
target_compile_features( rand PRIVATE c_std_11 )
target_compile_options( rand PRIVATE ${COMMON_CFLAGS} )

if(BUILD_R2P2)
  set(R2P2_TP_SOURCE "tp_r2p2.c")
  set(ENABLE_R2P2 "-DENABLE_R2P2")
endif()

if(R2P2_NIC_TS)
  set(ENABLE_R2P2_NIC_TS "-DR2P2_NIC_TS")
endif()

set(HTTP_SOURCES "http_app.cc" "picohttpparser.c")

add_executable( loader )
target_link_libraries( loader PRIVATE lancet )
target_sources( loader PRIVATE
        ${RAND_SRCS}
        "app_proto.c" "key_gen.c"
        "loader.c" "redis.c" "memcache.c"
        ${HTTP_SOURCES}
  )
target_link_libraries( loader PRIVATE rand )
target_include_directories( loader PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" )
target_compile_features( loader PUBLIC c_std_11 )
target_compile_features( loader PUBLIC cxx_std_14 )
target_compile_options( loader PRIVATE ${COMMON_CFLAGS} )
target_include_directories( loader PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" )

add_executable( agent )
target_sources( agent PRIVATE
        ${RAND_SRCS}
        "agent.c" "args.c"
        "app_proto.c"
        "tp_tcp.c" "tp_udp.c" "key_gen.c"
        "stats.c" "timestamping.c" "redis.c" "memcache.c"
        ${HTTP_SOURCES}
        ${R2P2_TP_SOURCE}
        )
target_compile_features( agent PUBLIC c_std_11 )
target_compile_features( agent PUBLIC cxx_std_14 )
target_include_directories( agent PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" )
target_link_libraries( agent PRIVATE lancet )
target_compile_options(agent PRIVATE ${COMMON_CFLAGS} ${ENABLE_R2P2} ${ENABLE_R2P2_NIC_TS})
target_link_libraries( agent PRIVATE
  Threads::Threads ${LIBM} ${LIBRT} )
if(BUILD_R2P2)
  target_link_libraries( agent PRIVATE r2p2 ${LIBCONFIG})
endif()
