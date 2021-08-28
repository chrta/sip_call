#!/bin/bash

# this script is expected to by executed by a build server from the top level of the project

set -eo pipefail

mkdir build_gcc
cd build_gcc
cmake ../native -G Ninja -D CMAKE_CXX_FLAGS=-DASIO_STANDALONE
ninja
cd ..
mkdir build_clang
cd build_clang
CC=clang-13 CXX=clang++-13 cmake ../native -G Ninja -D CMAKE_CXX_FLAGS=-DASIO_STANDALONE
ninja
cd ..
