#!/bin/bash

# this script is expected to by executed by a build server from the top level of the project

set -eo pipefail

idf.py fullclean
# build default config
idf.py build
idf.py fullclean

# build alternative config with some different settings
idf.py build -DSDKCONFIG_DEFAULTS="sdkconfig-ci-cfg-1.defaults"
idf.py fullclean
