#!/bin/bash

# this script is expected to by executed by a build server from the top level of the project

set -eo pipefail

ci/run-clang-format.py --clang-format-executable clang-format-12 -r main/ components/ native/
