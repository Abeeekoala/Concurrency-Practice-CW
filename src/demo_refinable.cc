#include "src/benchmark.h"
#include "src/hash_set_refinable.h"

int main(int argc, char** argv) {
  return benchmark::RunBenchmark<HashSetRefinable<int>>(argc, argv);
}
