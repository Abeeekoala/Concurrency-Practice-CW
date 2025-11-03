#!/usr/bin/env bash

set -e
set -u
set -x

./scripts/check_format.sh
./scripts/check_clean_build.sh

