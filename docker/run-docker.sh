#!/bin/bash

set -eo pipefail

. /etc/os-release

# Retry apt-get with up to 3 attempts to handle transient network/hash issues
apt_install() {
    local max_attempts=3
    local attempt=1
    while [ $attempt -le $max_attempts ]; do
        if apt-get update && apt-get install -y "$@"; then
            return 0
        fi
        attempt=$((attempt + 1))
        if [ $attempt -le $max_attempts ]; then
            echo "Attempt $attempt failed, retrying..."
            sleep 5
        fi
    done
    echo "Failed to install packages after $max_attempts attempts"
    return 1
}

apt_install \
	apt-transport-https \
	ca-certificates \
	gnupg \
	software-properties-common \
	wget \
	libmbedtls-dev \
	g++

wget -O - https://apt.llvm.org/llvm-snapshot.gpg.key | apt-key add -

echo deb http://apt.llvm.org/${UBUNTU_CODENAME}/ llvm-toolchain-${UBUNTU_CODENAME}-21 main > /etc/apt/sources.list.d/llvm.list

wget https://github.com/chriskohlhoff/asio/archive/asio-1-32-0.tar.gz -O /tmp/asio-1-32-0.tar.gz

tar -C /usr/include --strip-components=3 -x -f /tmp/asio-1-32-0.tar.gz asio-asio-1-32-0/asio/include/

apt_install \
    clang-21 \
	clang-format-21 \
	clang-tidy-21

# Clean up apt cache after all installations complete
rm -rf /var/lib/apt/lists/*
