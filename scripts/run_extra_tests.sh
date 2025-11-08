#!/usr/bin/env bash

set -euo pipefail
set -x

./scripts/check_build.sh

./temp/build-debug/hash_set_tests
