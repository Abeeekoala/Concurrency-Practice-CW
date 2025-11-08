#ifndef HASH_SET_STRIPED_H
#define HASH_SET_STRIPED_H

#include <cassert>
#include <functional>
#include <mutex>
#include <atomic>

#include "src/hash_set_base.h"

template <typename T>
class HashSetStriped : public HashSetBase<T> {
 public:
  explicit HashSetStriped(size_t initial_capacity)
      : table_(initial_capacity), mutexes_(initial_capacity) {
        size_.store(0);
      }

  bool Add(T elem) final {
    std::unique_lock<std::mutex> lock(
        mutexes_[MutexIndex(elem)]);  // Use unique_lock for manual unlocking

    size_t index = BucketIndex(elem);
    auto& bucket = table_[index];

    if (std::find(bucket.begin(), bucket.end(), elem) != bucket.end()) {
      return false;  // Element already present
    }

    bucket.push_back(elem);
    size_.fetch_add(1);

    if (policy()) {
      lock.unlock();
      resize();
    }

    return true;
  }

  bool Remove(T elem) final {
    std::scoped_lock<std::mutex> lock(mutexes_[MutexIndex(elem)]);

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
    std::scoped_lock<std::mutex> lock(mutexes_[MutexIndex(elem)]);

    size_t index = BucketIndex(elem);
    auto& bucket = table_[index];

    return std::find(bucket.begin(), bucket.end(), elem) != bucket.end();
  }

  [[nodiscard]] size_t Size() const final {
    return size_.load();
  }

 private:
  std::vector<std::vector<T>> table_;
  std::vector<std::mutex> mutexes_;
  std::atomic<std::size_t> size_;

  size_t BucketIndex(const T& elem) const {
    return std::hash<T>()(elem) % table_.size();
  }

  size_t MutexIndex(const T& elem) const {
    return std::hash<T>()(elem) % mutexes_.size();
  }

  bool policy() const { return size_.load() / table_.size() > 4; }

  void resize() {
    // Lock all mutexes
    for (auto& mtx : mutexes_) {
      mtx.lock();
    }

    size_t old_capacity = table_.size();
    size_t new_capacity = old_capacity * 2;

    std::vector<std::vector<T>> old_table = table_;
    table_.resize(new_capacity);
    for (size_t i = 0; i < new_capacity; i++) {
      table_[i] = std::vector<T>();
    }

    for (auto& bucket : old_table) {
      for (const T& elem : bucket) {
        size_t index = BucketIndex(elem);
        table_[index].push_back(elem);
      }
    }

    for (auto& mtx : mutexes_) {
      mtx.unlock();
    }
  }
};

#endif  // HASH_SET_STRIPED_H
