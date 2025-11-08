#include <atomic>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "src/hash_set_coarse_grained.h"
#include "src/hash_set_sequential.h"

namespace {

template <typename HashSet>
bool RunBasicContractTest(const std::string& label) {
  bool ok = true;
  HashSet set(8);

  if (!set.Add(1)) {
    std::cerr << label << ": expected first insert to succeed" << std::endl;
    ok = false;
  }
  if (set.Add(1)) {
    std::cerr << label << ": expected duplicate insert to fail" << std::endl;
    ok = false;
  }
  if (!set.Contains(1)) {
    std::cerr << label << ": expected element to be present" << std::endl;
    ok = false;
  }
  if (set.Contains(2)) {
    std::cerr << label << ": unexpected element present" << std::endl;
    ok = false;
  }
  if (set.Size() != 1u) {
    std::cerr << label << ": expected size to be 1, found " << set.Size()
              << std::endl;
    ok = false;
  }
  if (!set.Remove(1)) {
    std::cerr << label << ": expected removal to succeed" << std::endl;
    ok = false;
  }
  if (set.Remove(1)) {
    std::cerr << label << ": expected removal of missing element to fail"
              << std::endl;
    ok = false;
  }
  if (set.Size() != 0u) {
    std::cerr << label << ": expected size to be 0 after removals, found "
              << set.Size() << std::endl;
    ok = false;
  }

  return ok;
}

template <typename HashSet>
bool RunResizeBehaviourTest(const std::string& label) {
  bool ok = true;
  const size_t initial_capacity = 8;
  const size_t to_insert = initial_capacity * 6;

  HashSet set(initial_capacity);
  for (size_t i = 0; i < to_insert; ++i) {
    if (!set.Add(static_cast<int>(i))) {
      std::cerr << label << ": failed to insert element " << i << std::endl;
      ok = false;
      break;
    }
  }

  for (size_t i = 0; i < to_insert; ++i) {
    if (!set.Contains(static_cast<int>(i))) {
      std::cerr << label << ": missing element after resize " << i
                << std::endl;
      ok = false;
    }
  }

  if (set.Size() != to_insert) {
    std::cerr << label << ": expected size " << to_insert << ", found "
              << set.Size() << std::endl;
    ok = false;
  }

  return ok;
}

bool RunCoarseGrainedConcurrencyTest() {
  bool ok = true;
  constexpr size_t thread_count = 4;
  constexpr size_t per_thread = 256;
  constexpr size_t initial_capacity = 16;

  HashSetCoarseGrained<int> set(initial_capacity);
  std::atomic<bool> start{false};
  std::atomic<size_t> duplicates{0};

  std::vector<std::thread> threads;
  threads.reserve(thread_count);

  for (size_t t = 0; t < thread_count; ++t) {
    threads.emplace_back([&, t]() {
      while (!start.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }

      const size_t base = t * per_thread * 2;

      for (size_t i = 0; i < per_thread; ++i) {
        if (!set.Add(static_cast<int>(base + i))) {
          duplicates.fetch_add(1, std::memory_order_relaxed);
        }
      }

      for (size_t i = 0; i < per_thread / 2; ++i) {
        set.Remove(static_cast<int>(base + i));
      }
    });
  }

  start.store(true, std::memory_order_release);

  for (auto& thread : threads) {
    thread.join();
  }

  if (duplicates.load() != 0u) {
    std::cerr << "HashSetCoarseGrained concurrency: observed "
              << duplicates.load() << " unexpected duplicate insertions"
              << std::endl;
    ok = false;
  }

  const size_t expected_size = thread_count * (per_thread / 2);
  if (set.Size() != expected_size) {
    std::cerr << "HashSetCoarseGrained concurrency: expected size "
              << expected_size << ", found " << set.Size() << std::endl;
    ok = false;
  }

  for (size_t t = 0; t < thread_count; ++t) {
    const size_t base = t * per_thread * 2;
    for (size_t i = 0; i < per_thread / 2; ++i) {
      if (set.Contains(static_cast<int>(base + i))) {
        std::cerr << "HashSetCoarseGrained concurrency: found element that "
                     "should have been removed "
                  << (base + i) << std::endl;
        ok = false;
      }
    }
    for (size_t i = per_thread / 2; i < per_thread; ++i) {
      if (!set.Contains(static_cast<int>(base + i))) {
        std::cerr << "HashSetCoarseGrained concurrency: missing element "
                  << (base + i) << std::endl;
        ok = false;
      }
    }
  }

  return ok;
}

}  // namespace

int main() {
  bool ok = true;

  if (!RunBasicContractTest<HashSetSequential<int>>(
          "HashSetSequential basic contract")) {
    ok = false;
  }

  if (!RunResizeBehaviourTest<HashSetSequential<int>>(
          "HashSetSequential resize")) {
    ok = false;
  }

  if (!RunBasicContractTest<HashSetCoarseGrained<int>>(
          "HashSetCoarseGrained basic contract")) {
    ok = false;
  }

  if (!RunResizeBehaviourTest<HashSetCoarseGrained<int>>(
          "HashSetCoarseGrained resize")) {
    ok = false;
  }

  if (!RunCoarseGrainedConcurrencyTest()) {
    ok = false;
  }

  if (ok) {
    std::cout << "All additional hash set tests passed" << std::endl;
    return 0;
  }

  std::cerr << "Additional hash set tests FAILED" << std::endl;
  return 1;
}

