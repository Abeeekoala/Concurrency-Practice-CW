#!/usr/bin/env bash

set -e
set -u
set -x

./scripts/source_files.sh | xargs clang-format-18 -i --verbose
