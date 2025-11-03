#include "src/benchmark.h"
#include "src/hash_set_coarse_grained.h"

int main(int argc, char** argv) {
  return benchmark::RunBenchmark<HashSetCoarseGrained<int>>(argc, argv);
}
