#ifndef HASH_SET_STRIPED_H
#define HASH_SET_STRIPED_H

#include <algorithm>  // Needed for std::find
#include <atomic>
#include <cassert>
#include <functional>
#include <mutex>
#include <vector>  // Needed for std::vector

#include "src/hash_set_base.h"

template <typename T>
class HashSetStriped : public HashSetBase<T> {
 public:
  explicit HashSetStriped(size_t initial_capacity)
      : table_(std::max(initial_capacity, size_t(1))),
        // STRIPED: Fixed number of locks (doesn't grow with table)
        // This is the key property that distinguishes striped from
        // coarse-grained
        mutexes_(std::max(initial_capacity, size_t(1))),
        size_(0),
        num_locks_(std::max(initial_capacity, size_t(1))) {}

  bool Add(T elem) final {
    bool need_resize = false;

    {  // Scope for individual lock
      std::scoped_lock<std::mutex> lock(
          mutexes_[LockIndex(elem)]);  // Use unique_lock for manual unlocking

      size_t index = BucketIndex(elem);
      auto& bucket = table_[index];

      if (std::find(bucket.begin(), bucket.end(), elem) != bucket.end()) {
        return false;  // Element already present
      }

      bucket.push_back(elem);
      size_.fetch_add(1);
      // Check policy while still holding lock
      // This prevents race where another thread modifies bucket
      need_resize = policy();
      // lock is released here at end of scope
    }

    // Resize if needed (acquires ALL locks)
    // Must release individual lock first to avoid deadlock
    if (need_resize) {
      resize();
    }

    return true;
  }

  bool Remove(T elem) final {
    std::scoped_lock<std::mutex> lock(mutexes_[LockIndex(elem)]);

    size_t index = BucketIndex(elem);
    auto& bucket = table_[index];

    auto it = std::find(bucket.begin(), bucket.end(), elem);
    if (it == bucket.end()) {
      return false;  // Element not found
    }

    bucket.erase(it);
    size_.fetch_sub(1);
    return true;
  }

  [[nodiscard]] bool Contains(T elem) final {
    std::scoped_lock<std::mutex> lock(mutexes_[LockIndex(elem)]);

    size_t index = BucketIndex(elem);
    auto& bucket = table_[index];

    return std::find(bucket.begin(), bucket.end(), elem) != bucket.end();
  }

  [[nodiscard]] size_t Size() const final { return size_.load(); }

 private:
  std::vector<std::vector<T>> table_;
  std::vector<std::mutex> mutexes_;
  std::atomic<std::size_t> size_;
  const size_t num_locks_;  // Remember original size as locks are fixed

  size_t BucketIndex(const T& elem) const {
    return std::hash<T>()(elem) % table_.size();
  }

  // better naming from 'MutexIndex' to 'LockIndex' and using num_locks_
  size_t LockIndex(const T& elem) const {
    return std::hash<T>()(elem) % num_locks_;
  }

  bool policy() const {
    return table_.size() > 0 &&
           size_.load(std::memory_order_relaxed) > table_.size() * 4;
  }

  void resize() {
    // Create vector of unique_locks to lock all mutexes (RAII)
    std::vector<std::unique_lock<std::mutex>> locks;
    locks.reserve(num_locks_);

    for (size_t i = 0; i < num_locks_; ++i) {
      locks.emplace_back(mutexes_[i]);  // Lock accquired on construction
    }

    size_t old_capacity = table_.size();

    // Check again if resize still needed
    if (size_.load() <= old_capacity * 4) {
      return;
    }

    size_t new_capacity = old_capacity * 2;

    std::vector<std::vector<T>> old_table = std::move(table_);
    table_.resize(new_capacity);

    for (const auto& bucket : old_table) {
      for (const T& elem : bucket) {
        size_t index = BucketIndex(elem);
        table_[index].push_back(elem);
      }
    }

    // Locks automatically released when vector goes out of scope!
    // Even if exception thrown above, if we locked an unlocked we could be in a
    // deadlock!
  }
};

#endif  // HASH_SET_STRIPED_H
