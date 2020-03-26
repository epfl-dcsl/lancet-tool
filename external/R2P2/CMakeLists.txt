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
message(STATUS "Importing R2P2")
cmake_minimum_required(VERSION 3.12...3.13)

# TODO not sure if this is the best way to do this. have to double-specify R2P2_ROOT AND BUILD_R2P2
if(NOT R2P2_ROOT)
  message(FATAL_ERROR "importing R2P2 and R2P2_ROOT variable unspecified")
endif()

if(R2P2_NIC_TS)
  set(WITH_TIMESTAMPING "-DWITH_TIMESTAMPING")
endif()

message(STATUS "WITH TIMESTAMPING: " ${WITH_TIMESTAMPING})

project(r2p2 VERSION 1.0.0
  DESCRIPTION "R2P2"
  LANGUAGES C CXX)
message(STATUS "provided r2p2 root: ${R2P2_ROOT}")

if(IS_DIRECTORY ${R2P2_ROOT})
  get_filename_component(R2P2_ROOT_DIR ${R2P2_ROOT} ABSOLUTE BASE_DIR ${CMAKE_CURRENT_BINARY_DIR})
  message(STATUS "R2P2_ROOT_DIR: ${R2P2_ROOT_DIR}")
else()
  message(FATAL_ERROR "R2P2_ROOT does not point to a directory: ${R2P2_ROOT}")
endif()

file(GLOB r2p2_sources ${R2P2_ROOT_DIR}/r2p2-common.c ${R2P2_ROOT_DIR}/mempool.c ${R2P2_ROOT_DIR}/cfg.c ${R2P2_ROOT_DIR}/linux-backend.c ${R2P2_ROOT_DIR}/timestamping.c)

add_library( r2p2 OBJECT ${r2p2_sources} ) # Not sure if this or static
target_include_directories( r2p2 PUBLIC ${R2P2_ROOT_DIR}/inc)
target_compile_features( r2p2 PUBLIC c_std_11 )
target_compile_options(r2p2 PUBLIC "-DLINUX") # PUBLIC = it will propagate as a compile option to all dependencies, too
target_compile_options(r2p2 PUBLIC ${WITH_TIMESTAMPING})
