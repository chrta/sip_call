#!/bin/bash

# this script is expected to by executed by a build server from the top level of the project

set -eo pipefail

targets="esp32 esp32s2 esp32c3"

for t in $targets; do
  idf.py fullclean
  idf.py set-target $t
  # build default config
  idf.py build
  idf.py fullclean

  # build alternative config with some different settings
  idf.py build -DSDKCONFIG_DEFAULTS="sdkconfig-ci-cfg-1.defaults"
  idf.py fullclean
done
