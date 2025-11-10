#ifndef HASH_SET_SEQUENTIAL_H
#define HASH_SET_SEQUENTIAL_H

#include <algorithm>
#include <cassert>
#include <functional>
#include <vector>

#include "src/hash_set_base.h"

template <typename T>
class HashSetSequential : public HashSetBase<T> {
 public:
  // Ensure capacity is at least 1 to prevent division by zero
  explicit HashSetSequential(size_t initial_capacity)
      : table_(std::max(initial_capacity, size_t(1))), size_(0) {}

  bool Add(T elem) final {
    size_t index = BucketIndex(elem);
    auto& bucket = table_[index];

    if (std::find(bucket.begin(), bucket.end(), elem) != bucket.end()) {
      return false;  // Element already present
    }

    bucket.push_back(elem);
    size_++;

    if (policy()) {
      resize();
    }

    return true;
  }

  bool Remove(T elem) final {
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
    size_t index = BucketIndex(elem);
    auto& bucket = table_[index];

    return std::find(bucket.begin(), bucket.end(), elem) != bucket.end();
  }

  [[nodiscard]] size_t Size() const final { return size_; }

 private:
  std::vector<std::vector<T>> table_;
  size_t size_;

  size_t BucketIndex(const T& elem) const {
    return std::hash<T>()(elem) % table_.size();
  }

  // Resize policy: resize when average bucket size exceeds 4
  // Added safety check even though constructor ensures table_.size() >= 1
  bool policy() const { return table_.size() > 0 && size_ / table_.size() > 4; }

  // Resize: doubles capacity and rehashes all elements
  void resize() {
    size_t old_capacity = table_.size();
    size_t new_capacity = old_capacity * 2;

    // Move old table efficiently (no copy)
    std::vector<std::vector<T>> old_table = std::move(table_);

    // Create new table with doubled capacity
    table_.resize(new_capacity);

    // Rehash all elements from old table
    for (const auto& bucket : old_table) {
      for (const T& elem : bucket) {
        size_t index = BucketIndex(elem);
        table_[index].push_back(elem);
      }
    }
  }
};

#endif  // HASH_SET_SEQUENTIAL_H
