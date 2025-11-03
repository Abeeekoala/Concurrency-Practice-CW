#include "src/benchmark.h"

namespace benchmark {

void ThreadBody(HashSetBase<int>& hash_set, size_t chunk_size, size_t id,
                size_t& max_observed_size) {
  max_observed_size = 0;
  for (size_t k = 0; k < chunk_size * 2; k++) {
    int elem = static_cast<int>(id * chunk_size + k);
    hash_set.Add(elem);
    max_observed_size = std::max(max_observed_size, hash_set.Size());
  }
  for (size_t j = 0; j < 20; j++) {
    for (size_t k = 0; k < chunk_size * 2; k++) {
      int elem = static_cast<int>(id * chunk_size + k);
      if (hash_set.Contains(elem)) {
        if ((elem % 20) == 0) {
          hash_set.Remove(elem);
          max_observed_size = std::max(max_observed_size, hash_set.Size());
        }
      }
    }
  }
  for (size_t k = 0; k < chunk_size * 2; k++) {
    int elem = static_cast<int>(id * chunk_size + k);
    hash_set.Add(elem);
    max_observed_size = std::max(max_observed_size, hash_set.Size());
  }
}

}  // namespace benchmark
