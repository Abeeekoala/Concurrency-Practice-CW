#include "src/benchmark.h"
#include "src/hash_set_striped.h"

int main(int argc, char** argv) {
  return benchmark::RunBenchmark<HashSetStriped<int>>(argc, argv);
}
