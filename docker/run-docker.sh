#!/bin/bash

set -eo pipefail

. /etc/os-release

apt-get update \
    && apt-get install -y \
	       apt-transport-https \
	       ca-certificates \
	       gnupg \
	       software-properties-common \
	       wget \
	       libmbedtls-dev \
	       g++

# wget -O - https://apt.kitware.com/keys/kitware-archive-latest.asc 2>/dev/null | apt-key add -
# apt-add-repository 'deb https://apt.kitware.com/ubuntu/ xenial main'

#apt-get update \
	#    && apt-get install -y cmake \

wget -O - https://apt.llvm.org/llvm-snapshot.gpg.key | apt-key add -

echo deb http://apt.llvm.org/${UBUNTU_CODENAME}/ llvm-toolchain-${UBUNTU_CODENAME}-15 main > /etc/apt/sources.list.d/llvm.list

wget https://github.com/chriskohlhoff/asio/archive/asio-1-14-1.tar.gz -O /tmp/asio-1-14-1.tar.gz

tar -C /usr/include --strip-components=3 -x -f /tmp/asio-1-14-1.tar.gz asio-asio-1-14-1/asio/include/

apt-get update \
    && apt-get install -y clang-15 \
	       clang-format-15 \
	       clang-tidy-15 \
    && rm -rf /var/lib/apt/lists/*
