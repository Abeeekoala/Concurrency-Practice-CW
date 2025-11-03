#!/usr/bin/env bash

set -e
set -u
set -x

test -d src/
test -d temp/

cd temp

mkdir -p build-tsan
pushd build-tsan

cmake -G "Unix Makefiles" ../.. -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=clang++-18 -DUSE_SANITIZER=tsan -DCMAKE_CXX_FLAGS="-stdlib=libc++"
cmake --build . --config Debug --parallel

popd


mkdir -p build-asan
pushd build-asan

cmake -G "Unix Makefiles" ../.. -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=clang++-18 -DUSE_SANITIZER=asan -DCMAKE_CXX_FLAGS="-stdlib=libc++"
cmake --build . --config Debug --parallel

popd


mkdir -p build-release
pushd build-release

cmake -G "Unix Makefiles" ../.. -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++-18 -DCMAKE_CXX_FLAGS="-stdlib=libc++"
cmake --build . --config Release --parallel

popd

mkdir -p build-debug
pushd build-debug

cmake -G "Unix Makefiles" ../.. -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=clang++-18 -DCMAKE_CXX_FLAGS="-stdlib=libc++"
cmake --build . --config Debug --parallel

popd
