#!/usr/bin/env bash

set -e
set -u
set -x

test -d src/
test -d temp/

rm -rf temp/build-*

./scripts/check_build.sh
