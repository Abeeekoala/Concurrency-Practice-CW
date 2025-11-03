#ifndef BENCHMARK_H
#define BENCHMARK_H

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <iostream>
#include <thread>
#include <vector>

#include "src/hash_set_base.h"

namespace benchmark {

void ThreadBody(HashSetBase<int>& hash_set, size_t chunk_size, size_t id,
                size_t& max_observed_size);

template <typename HashSetType>
int RunBenchmark(int argc, char** argv) {
  if (argc != 4) {
    std::cerr << "Usage: " << argv[0]
              << " num_threads initial_capacity chunk_size" << std::endl;
    return 1;
  }
  size_t num_threads = std::stoul(std::string(argv[1]));
  size_t initial_capacity = std::stoul(std::string(argv[2]));
  size_t chunk_size = std::stoul(std::string(argv[3]));

  HashSetType hash_set(initial_capacity);

  std::vector<size_t> max_observed_sizes;
  max_observed_sizes.reserve(num_threads);
  for (size_t i = 0; i < num_threads; i++) {
    max_observed_sizes.emplace_back(0u);
  }

  std::vector<std::thread> threads;
  threads.reserve(num_threads);

  auto begin_time = std::chrono::high_resolution_clock::now();
  for (size_t i = 0; i < num_threads; i++) {
    threads.emplace_back(std::thread(ThreadBody, std::ref(hash_set), chunk_size,
                                     i, std::ref(max_observed_sizes.at(i))));
  }
  for (auto& thread : threads) {
    thread.join();
  }
  auto end_time = std::chrono::high_resolution_clock::now();

  auto duration = end_time - begin_time;
  auto millis =
      std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();

  size_t expected_size = chunk_size * (num_threads + 1);
  if (hash_set.Size() != expected_size) {
    std::cerr << argv[0] << " failed: size " << hash_set.Size()
              << " does not match expected size " << expected_size << std::endl;
    return 1;
  }
  for (size_t i = 0; i < chunk_size * (num_threads + 1); i++) {
    int expected_value = static_cast<int>(i);
    if (!hash_set.Contains(expected_value)) {
      std::cerr << argv[0] << " failed: expected value " << expected_value
                << " not found" << std::endl;
      return 1;
    }
  }

  std::cout << argv[0] << " succeeded" << std::endl;
  std::cout << "Concurrent computation took:" << std::endl;
  std::cout << "  " << millis << " ms" << std::endl;
  return 0;
}

}  // namespace benchmark

#endif  // BENCHMARK_H
