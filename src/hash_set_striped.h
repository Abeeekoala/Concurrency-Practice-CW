#ifndef HASH_SET_STRIPED_H
#define HASH_SET_STRIPED_H

#include <algorithm>  // For std::find
#include <atomic>
#include <cassert>
#include <functional>
#include <mutex>
#include <vector>

#include "src/hash_set_base.h"

template <typename T>
class HashSetStriped : public HashSetBase<T> {
 public:
  explicit HashSetStriped(size_t initial_capacity)
      : table_(std::max(initial_capacity, kMinCapacity)),
        mutexes_(std::max(initial_capacity, kMinCapacity)),
        size_(0),
        num_locks_(std::max(initial_capacity, kMinCapacity)) {}

  bool Add(T elem) final {
    bool needs_resize = false;

    {  // Scope for lock lifetime management (RAII pattern)
      std::scoped_lock lock(mutexes_[GetLockIndex(elem)]);

      auto& bucket = GetBucket(elem);

      if (BucketContains(bucket, elem)) {
        return false;  // Element already exists
      }

      bucket.push_back(elem);
      IncrementSize();

      needs_resize = ShouldResize();
    }  // Lock automatically released here

    if (needs_resize) {
      Resize();
    }

    return true;
  }

  bool Remove(T elem) final {
    std::scoped_lock lock(mutexes_[GetLockIndex(elem)]);

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
    std::scoped_lock lock(mutexes_[GetLockIndex(elem)]);

    const auto& bucket = GetBucket(elem);
    return BucketContains(bucket, elem);
  }

  [[nodiscard]] size_t Size() const final {
    return size_.load(std::memory_order_relaxed);
  }

 private:
  // Configuration constants
  static constexpr size_t kMinCapacity = 1;
  static constexpr size_t kResizeFactor = 2;
  static constexpr size_t kMaxLoadFactor = 4;

  // Member variables
  std::vector<std::vector<T>> table_;  // Hash table buckets
  std::vector<std::mutex> mutexes_;    // Fixed-size lock array (striped)
  std::atomic<size_t> size_;           // Total element count
  const size_t num_locks_;             // Number of locks (constant)

  //=============================================================================
  // Hash and Index Computation
  //=============================================================================

  /**
   * Computes the bucket index for a given element.
   * Changes when table resizes.
   */
  [[nodiscard]] size_t GetBucketIndex(const T& elem) const {
    return ComputeHash(elem) % GetCapacity();
  }

  /**
   * Computes the lock index for a given element.
   * NEVER changes - this is the key property of striped hashing.
   */
  [[nodiscard]] size_t GetLockIndex(const T& elem) const {
    return ComputeHash(elem) % num_locks_;
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
  [[nodiscard]] bool BucketContains(const std::vector<T>& bucket,
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
   * Atomically increments the element count.
   */
  void IncrementSize() { size_.fetch_add(1, std::memory_order_relaxed); }

  /**
   * Atomically decrements the element count.
   */
  void DecrementSize() { size_.fetch_sub(1, std::memory_order_relaxed); }

  /**
   * Gets current element count.
   */
  [[nodiscard]] size_t GetSize() const {
    return size_.load(std::memory_order_relaxed);
  }

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
   * 2. Division by zero
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
   * Thread Safety:
   * - Acquires ALL locks to ensure exclusive access
   * - Uses RAII (unique_lock) for exception safety
   * - Rechecks resize condition after acquiring locks (guard pattern)
   */
  void Resize() {
    // Step 1: Acquire all locks using RAII pattern
    std::vector<std::unique_lock<std::mutex>> all_locks;
    all_locks.reserve(num_locks_);

    for (size_t i = 0; i < num_locks_; ++i) {
      all_locks.emplace_back(mutexes_[i]);
    }

    // Step 2: Check if resize is still needed (double-check pattern)
    // Another thread might have already resized while we waited for locks
    const size_t old_capacity = GetCapacity();
    if (!ShouldResize()) {
      return;  // Resize no longer needed
    }

    // Step 3: Create new table with doubled capacity
    const size_t new_capacity = old_capacity * kResizeFactor;
    std::vector<std::vector<T>> old_table = std::move(table_);
    table_.resize(new_capacity);

    // Step 4: Rehash all elements into new table
    RehashElements(old_table);

    // Step 5: Locks automatically released when all_locks goes out of scope
    //         This is exception-safe - even if rehashing throws, locks release
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

#endif  // HASH_SET_STRIPED_H
