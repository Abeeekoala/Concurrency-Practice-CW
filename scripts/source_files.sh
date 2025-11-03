#!/usr/bin/env bash

set -e
set -u
set -x

test -d src/

find ./src/ '(' -iname '*.cc' -o -iname '*.h' ')' -print
