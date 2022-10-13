#!/bin/bash

# this script is expected to by executed by a build server from the top level of the project

set -eo pipefail

mkdir build_clang
cd build_clang
CC=clang-15 CXX=clang++-15 cmake ../native -G Ninja -D CMAKE_CXX_FLAGS=-DASIO_STANDALONE -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

run-clang-tidy-15.py -clang-tidy-binary clang-tidy-15
