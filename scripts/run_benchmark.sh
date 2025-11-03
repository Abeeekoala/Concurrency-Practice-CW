#!/usr/bin/env bash

set -e
set -u
set -x

./scripts/check_build.sh

./temp/build-release/demo_coarse_grained 8 4 100000
./temp/build-release/demo_striped 8 4 100000
./temp/build-release/demo_refinable 8 4 100000
