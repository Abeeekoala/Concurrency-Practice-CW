#ifndef HASH_SET_COARSE_GRAINED_H
#define HASH_SET_COARSE_GRAINED_H

#include <cassert>
#include <functional>
#include <mutex>

#include "src/hash_set_base.h"

template <typename T>
class HashSetCoarseGrained : public HashSetBase<T> {
 public:
  explicit HashSetCoarseGrained(size_t initial_capacity)
      : table_(initial_capacity), size_(0) {}

  bool Add(T elem) final {
    std::unique_lock<std::mutex> lock(
        mutex_);  // Use unique_lock for manual unlocking

    size_t index = BucketIndex(elem);
    auto& bucket = table_[index];

    if (std::find(bucket.begin(), bucket.end(), elem) != bucket.end()) {
      return false;  // Element already present
    }

    bucket.push_back(elem);
    size_++;

    if (policy()) {
      lock.unlock();
      resize();
    }

    return true;
  }

  bool Remove(T elem) final {
    std::scoped_lock<std::mutex> lock(mutex_);

    size_t index = BucketIndex(elem);
    auto& bucket = table_[index];

    auto it = std::find(bucket.begin(), bucket.end(), elem);
    if (it == bucket.end()) {
      return false;  // Element not found
    }

    bucket.erase(it);
    size_--;
    return true;
  }

  [[nodiscard]] bool Contains(T elem) final {
    std::scoped_lock<std::mutex> lock(mutex_);

    size_t index = BucketIndex(elem);
    auto& bucket = table_[index];

    return std::find(bucket.begin(), bucket.end(), elem) != bucket.end();
  }

  [[nodiscard]] size_t Size() const final {
    std::scoped_lock<std::mutex> lock(mutex_);
    return size_;
  }

 private:
  std::vector<std::vector<T>> table_;
  size_t size_;
  mutable std::mutex mutex_;

  size_t BucketIndex(const T& elem) const {
    return std::hash<T>()(elem) % table_.size();
  }

  bool policy() const { return size_ / table_.size() > 4; }

  void resize() {
    std::scoped_lock<std::mutex> lock(mutex_);

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
  }
};

#endif  // HASH_SET_COARSE_GRAINED_H
