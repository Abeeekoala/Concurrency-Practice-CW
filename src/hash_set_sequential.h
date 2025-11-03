#ifndef HASH_SET_SEQUENTIAL_H
#define HASH_SET_SEQUENTIAL_H

#include <cassert>
#include <functional>

#include "src/hash_set_base.h"

template <typename T>
class HashSetSequential : public HashSetBase<T> {
 public:
  explicit HashSetSequential(size_t initial_capacity)
      : table_(initial_capacity), size_(0) {}

  bool Add(T elem) final {
    size_t index = BucketIndex(elem);
    auto& bucket = table_[index];

    if (std::find(bucket.begin(), bucket.end(), elem) != bucket.end()) {
      return false;  // Element already present
    }

    bucket.push_back(elem);
    size_++;
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
};

#endif  // HASH_SET_SEQUENTIAL_H
