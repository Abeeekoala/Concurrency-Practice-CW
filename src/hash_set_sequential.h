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
  explicit HashSetSequential(size_t initial_capacity)
      : table_(std::max(initial_capacity, kMinCapacity)), size_(0) {}

  bool Add(T elem) final {
    auto& bucket = GetBucket(elem);

    if (Contains(bucket, elem)) {
      return false;  // Element already present
    }

    bucket.push_back(elem);
    IncrementSize();

    if (ShouldResize()) {
      Resize();
    }

    return true;
  }

  bool Remove(T elem) final {
    auto& bucket = GetBucket(elem);

    auto it = std::find(bucket.begin(), bucket.end(), elem);
    if (it == bucket.end()) {
      return false;  // Element not found
    }

    bucket.erase(it);
    DecrementSize();
    return true;
  }

  [[nodiscard]] bool Contains(T elem) final {
    const auto& bucket = GetBucket(elem);
    return Contains(bucket, elem);
  }

  [[nodiscard]] size_t Size() const final { return size_; }

 private:
  // Configuration constants
  static constexpr size_t kMinCapacity = 1;
  static constexpr size_t kResizeFactor = 2;
  static constexpr size_t kMaxLoadFactor = 4;

  // Member variables
  std::vector<std::vector<T>> table_;  // Hash table buckets
  size_t size_;                        // Total element count

  //=============================================================================
  // Hash and Index Computation
  //=============================================================================

  /**
   * Computes the bucket index for a given element.
   */
  [[nodiscard]] size_t GetBucketIndex(const T& elem) const {
    return ComputeHash(elem) % GetCapacity();
  }

  /**
   * Computes hash value for an element.
   */
  [[nodiscard]] size_t ComputeHash(const T& elem) const {
    return std::hash<T>()(elem);
  }

  //=============================================================================
  // Bucket Access
  //=============================================================================

  /**
   * Gets the bucket for a given element (mutable).
   */
  [[nodiscard]] std::vector<T>& GetBucket(const T& elem) {
    return table_[GetBucketIndex(elem)];
  }

  /**
   * Gets the bucket for a given element (const).
   */
  [[nodiscard]] const std::vector<T>& GetBucket(const T& elem) const {
    return table_[GetBucketIndex(elem)];
  }

  /**
   * Checks if a bucket contains an element.
   */
  [[nodiscard]] bool Contains(const std::vector<T>& bucket,
                              const T& elem) const {
    return std::find(bucket.begin(), bucket.end(), elem) != bucket.end();
  }

  //=============================================================================
  // Capacity and Size Management
  //=============================================================================

  /**
   * Returns current table capacity (number of buckets).
   */
  [[nodiscard]] size_t GetCapacity() const { return table_.size(); }

  /**
   * Increments the element count.
   */
  void IncrementSize() { ++size_; }

  /**
   * Decrements the element count.
   */
  void DecrementSize() { --size_; }

  /**
   * Gets current element count.
   */
  [[nodiscard]] size_t GetSize() const { return size_; }

  //=============================================================================
  // Resize Policy and Operations
  //=============================================================================

  /**
   * Determines if table should be resized.
   * Resize when load factor exceeds kMaxLoadFactor.
   *
   * Load Factor = Total Elements / Number of Buckets
   *
   * We use multiplication instead of division to avoid:
   * 1. Integer truncation errors
   * 2. Division by zero (though constructor ensures capacity >= 1)
   * 3. Slower performance (division is ~10x slower than multiplication)
   */
  [[nodiscard]] bool ShouldResize() const {
    const size_t capacity = GetCapacity();
    const size_t current_size = GetSize();

    // Check: size > capacity * kMaxLoadFactor
    // This is equivalent to: size/capacity > kMaxLoadFactor
    return capacity > 0 && current_size > capacity * kMaxLoadFactor;
  }

  /**
   * Resizes the table to double capacity and rehashes all elements.
   *
   * Thread Safety: Not thread-safe (sequential implementation).
   */
  void Resize() {
    const size_t old_capacity = GetCapacity();
    const size_t new_capacity = old_capacity * kResizeFactor;

    // Move old table efficiently (O(1) operation, no copy)
    std::vector<std::vector<T>> old_table = std::move(table_);

    // Create new table with doubled capacity
    table_.resize(new_capacity);

    // Rehash all elements from old table
    RehashElements(old_table);
  }

  /**
   * Rehashes all elements from old table into current table.
   */
  void RehashElements(const std::vector<std::vector<T>>& old_table) {
    for (const auto& old_bucket : old_table) {
      for (const T& elem : old_bucket) {
        const size_t new_index = GetBucketIndex(elem);
        table_[new_index].push_back(elem);
      }
    }
  }
};

#endif  // HASH_SET_SEQUENTIAL_H
