#ifndef HASH_SET_REFINABLE_H
#define HASH_SET_REFINABLE_H

#include <algorithm> // For std::find
#include <cassert>
#include <functional>
#include <mutex>
#include <atomic>
#include <thread>
#include <vector>
#include <cstdint>

#include "src/hash_set_base.h"

// global registry for all Hazard Pointers (HP)
class HPRegistry {
 public:
  struct HazardNode {
      std::atomic<void*> hp{nullptr};
      std::atomic<HazardNode*> next{nullptr};
      bool active{true};
    };

  // on construction of thread_local HP
  HazardNode* registerHP() {
    auto* node = new HazardNode();
    HazardNode* expected = head_.load(std::memory_order_acquire);
    // lock-free push front
    do {
      node->next.store(expected, std::memory_order_relaxed);
    } while (!head_.compare_exchange_weak(
        expected,
        node,
        std::memory_order_release,   // success: release
        std::memory_order_relaxed)); // fail: relax
    return node;
  }

  // on destruction of thread_local HP
  void unregisterHP(HazardNode* node) {
    node->active = false;
    node->hp.store(nullptr, std::memory_order_release);
  }

  // for Resize() to get a list of all active pointers
  std::vector<void*> getAllHazardousPtrs() {
    std::vector<void*> result;
    HazardNode* current = head_.load(std::memory_order_acquire);
    
    while (current != nullptr) {
      if (current->active) {
        void* ptr = current->hp.load(std::memory_order_acquire);
        if (ptr != nullptr)
          result.push_back(ptr);
      }
      current = current->next.load(std::memory_order_acquire);
    }
    return result;
  }

  private:
    std::atomic<HazardNode*> head_{nullptr};

};

// thread_local Hazard Pointer
class HazardPointer {
  HPRegistry& registry_;
  HPRegistry::HazardNode* node_;
  
  public:
  explicit HazardPointer(HPRegistry& registry)
      : registry_(registry), 
        node_(registry_.registerHP()) {}

  ~HazardPointer() {
    registry_.unregisterHP(node_);
  }

  void set(void* ptr) noexcept {
    node_->hp.store(ptr, std::memory_order_release);
  }

  void clear() noexcept {
    node_->hp.store(nullptr, std::memory_order_release);
  }
};

template <typename T>
class HashSetRefinable : public HashSetBase<T> {
 public:
  explicit HashSetRefinable(size_t initial_capacity) {
    const size_t capacity = std::max(initial_capacity, kMinCapacity);
    table_.store(new std::vector<std::vector<T>>(capacity), std::memory_order_relaxed);
    locks_.store(new std::vector<std::mutex>(capacity), std::memory_order_relaxed);
    
    size_.store(0);
    owner_.store(pack(nullptr, false));

  }
  
  // Desctuctor: delete raw pointers
  ~HashSetRefinable() override {
    delete table_.load();
    delete locks_.load();
  }

  // Locks the relevant bucket, inserts if absent, and may trigger a resize
  bool Add(T elem) final {
    std::unique_lock<std::mutex> lock = Acquire(elem);

    auto curTable = table_.load(std::memory_order_relaxed);
    auto& bucket = GetBucket(curTable, elem);
    
    if (BucketContains(bucket, elem)) {
      return false;  // Element already exists
    }
    
    bool needs_resize = false;
    bucket.push_back(elem);
    IncrementSize();

    needs_resize = ShouldResize();

    if (needs_resize) {
      lock.unlock();
      Resize();
    }

    return true;
  }
  // Locks the bucket, erases the element when found, and updates the size.
  bool Remove(T elem) final {
    std::unique_lock<std::mutex> lock = Acquire(elem);

    auto curTable = table_.load(std::memory_order_relaxed);
    auto& bucket = GetBucket(curTable, elem);

    auto it = std::find(bucket.begin(), bucket.end(), elem);
    if (it == bucket.end()) {
      return false;  // Element not found
    }

    bucket.erase(it);
    DecrementSize();
    return true;
  }
  
  // Acquires the bucket lock to safely check membership.
  [[nodiscard]] bool Contains(T elem) final {
    std::unique_lock<std::mutex> lock = Acquire(elem);

    auto curTable = table_.load(std::memory_order_relaxed);
    auto& bucket = GetBucket(curTable, elem);

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

  // Tracks deferred reclamation targets (old lock arrays)
  struct RetiredNode {
    void* ptr;
    RetiredNode* next;
  };
  
  // Member variabes
  std::atomic<RetiredNode*> retired_head{nullptr};            // linked list of deferred reclamation targets
  std::atomic<std::vector<std::vector<T>>*> table_{nullptr};  // Hash table buckets
  std::atomic<std::vector<std::mutex>*> locks_{nullptr};      // Lock array
  std::atomic<std::size_t> size_{0};                          // Total element count
  std::atomic<uintptr_t> owner_{0};                           // Owner field (thread_ptr, mark): Bit steal hack

  // thread_local member variables
  inline static thread_local int dummy_thread_id_{};   // Location will be unique for each thread

  //Helper function to get thread token
  static void* GetThreadToken() {
    return &dummy_thread_id_;
  }

  // Hazard pointer helper functions
  static HPRegistry& Registry() {
    static HPRegistry instance;
    return instance;
  }

  static HazardPointer& getThreadHP() {
    static thread_local HazardPointer* hp = new HazardPointer(Registry());
    return *hp;
  }

  class OwnerGuard {
    std::atomic<uintptr_t>& owner_;
    bool is_owner_ = false;
   public:
    OwnerGuard(std::atomic<uintptr_t>& owner, void* me) : owner_(owner) {
      uintptr_t expected = pack(nullptr, false);
      uintptr_t desired = pack(me, true);
      is_owner_ = owner_.compare_exchange_strong(expected, desired,
                                               std::memory_order_acq_rel,
                                               std::memory_order_acquire);
    }
    ~OwnerGuard() {
      if (is_owner_) {
        owner_.store(pack(nullptr, false), std::memory_order_release);
      }
    }
    explicit operator bool() const { return is_owner_; }
  };

  // Owner Bit stealing:
  // pack bool:mark with pointer to thread to achieve lock free atomic
  // pointer usually ends with 2 (or 3) bits of zeros,
  // depending on the memory alignment,
  // making the LSB available for mark -> 0x... 1000, 1 => 0x... 1001
  // Helper functions for owner field
  static uintptr_t pack(void* ptr, bool mark) {
    return reinterpret_cast<uintptr_t>(ptr) | (mark ? 1 : 0);
  }

  static void* getPointer(uintptr_t packedValue) {
    // clear the mark for memory alignment
    return reinterpret_cast<void*>(packedValue & ~static_cast<uintptr_t>(1));
  }

  static bool getMark(uintptr_t packedValue) {
    // return mark (LSB)
    return (packedValue & 1) == 1;
  }
  
  //=============================================================================
  // Lock Bucket for element
  //=============================================================================

  std::unique_lock<std::mutex> Acquire(const T& elem) {
    HazardPointer& hp = getThreadHP();

    while (true) {
      // 1. Check for resize (fast spin, no RMW)
      do {
        uintptr_t packed = owner_.load(std::memory_order_acquire);
        if (getMark(packed) && getPointer(packed) != GetThreadToken()) {
          std::this_thread::yield();
        } else {
          break; // Not resizing, or we are the resizer
        }
      } while (true);
      
      // Get locks
      std::vector<std::mutex>* curLocks = nullptr;
      do {
        curLocks = locks_.load(std::memory_order_acquire);
        hp.set(curLocks); // Publish to our HP slot
      } while (locks_.load(std::memory_order_acquire) != curLocks);

      // try to lock
      std::mutex& m = (*curLocks)[MutexIndex(elem, curLocks->size())];
      std::unique_lock<std::mutex> lock(m);

      // Second check (no resizing) and (lock_ array is the same)
      uintptr_t packedAfter = owner_.load(std::memory_order_acquire);
      void* curOwner = getPointer(packedAfter);
      bool isMarked = getMark(packedAfter);

      if ((!isMarked || curOwner == GetThreadToken()) &&
            locks_.load(std::memory_order_acquire) == curLocks){
        hp.clear();
        return lock;
      }
      // Retry until succeed
      hp.clear();
    }
  }

  //=============================================================================
  // Capacity and Size Management
  //=============================================================================

  /**
   * Returns current table capacity (number of buckets).
   */
  [[nodiscard]] size_t GetCapacity() const {
    auto* current_table = table_.load(std::memory_order_acquire);
    return current_table ? current_table->size() : 0;
  }

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
  // Hash and Index Computation
  //=============================================================================

  /**
   * Computes hash value for an element.
   */
  [[nodiscard]] size_t ComputeHash(const T& elem) const {
    return std::hash<T>()(elem);
  }

  /**
   * Helper to compute the bucket index for a given element and capacity.
   */
  [[nodiscard]] size_t BucketIndex(const T& elem, size_t capacity) const {
    assert(capacity > 0);
    return ComputeHash(elem) % capacity;
  }

  /**
   * Helper to compute the mutex index for a given element and number of locks.
   */
  [[nodiscard]] size_t MutexIndex(const T& elem, size_t num_locks) const {
    assert(num_locks > 0);
    return ComputeHash(elem) % num_locks;
  }

  //=============================================================================
  // Bucket Access
  //=============================================================================

  /**
   * Gets the bucket for a given element (mutable).
   */
  [[nodiscard]] std::vector<T>& GetBucket(std::vector<std::vector<T>>* curTable, const T& elem) {
    assert(curTable != nullptr);
    return (*curTable)[BucketIndex(elem, curTable->size())];
  }
  /**
   * Gets the bucket for a given element (const).
   */
  [[nodiscard]] const std::vector<T>& GetBucket(
      const std::vector<std::vector<T>>* curTable, const T& elem) const {
    assert(curTable != nullptr);
    return (*curTable)[BucketIndex(elem, curTable->size())];
  }

  /**
   * Checks if a bucket contains an element.
   */
  [[nodiscard]] bool BucketContains(const std::vector<T>& bucket, const T& elem) const {
    return std::find(bucket.begin(), bucket.end(), elem) != bucket.end();
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

  void Resize() {
    // Resizer claims ownership
    OwnerGuard guard(owner_, GetThreadToken());
    if (!guard) {
      return; // another thread is resizing
    }

    auto old_table_ptr = table_.load(std::memory_order_relaxed);
    auto old_locks_ptr = locks_.load(std::memory_order_acquire);

    size_t old_capacity = old_table_ptr->size();
    
    // check if other already resized
    if (old_table_ptr != table_.load(std::memory_order_relaxed)) {
        return; 
    }
    
    // wait for all threads complete and release locks
    Quiesce();

    size_t new_capacity = std::max(old_capacity * 2, kMinCapacity);
    // new pointer after resize
    auto new_table_ptr = new std::vector<std::vector<T>>(new_capacity);
    auto new_locks_ptr = new std::vector<std::mutex>(new_capacity);
    
    // Re-hash elements 
    for (const auto& bucket : *old_table_ptr) {
        for (const T& elem : bucket) {
            (*new_table_ptr)[BucketIndex(elem, new_capacity)].push_back(elem);
        }
    }

    table_.store(new_table_ptr, std::memory_order_release);
    locks_.store(new_locks_ptr, std::memory_order_release);

    // return ownership
    Reclaim(old_locks_ptr);
  }
  
  /**
   * Reclaim old lock array once no hazard pointers reference it.
   */
  void Reclaim(std::vector<std::mutex>* old_locks_ptr) {
      // 1. Push this pointer into the global retired list (lock-free)
      auto* node = new RetiredNode{old_locks_ptr, nullptr};
      RetiredNode* expected = retired_head.load(std::memory_order_acquire);
      do {
          node->next = expected;
      } while (!retired_head.compare_exchange_weak(
                  expected,
                  node,
                  std::memory_order_release,
                  std::memory_order_relaxed));

      // 2. Get a snapshot of all hazardous pointers
      std::vector<void*> hazardous_ptrs = Registry().getAllHazardousPtrs();

      auto isHazardous = [&](void* candidate) {
          for (void* hp : hazardous_ptrs) {
              if (hp == candidate) return true;
          }
          return false;
      };

      // 3. Traverse the retired list and reclaim non-hazardous items
      RetiredNode* prev = nullptr;
      RetiredNode* cur  = retired_head.load(std::memory_order_acquire);

      while (cur) {
          if (!isHazardous(cur->ptr)) {
              // Remove this node from the list
              RetiredNode* next = cur->next;
              if (prev) {
                  prev->next = next;
              } else {
                  // update head_
                  RetiredNode* expected_head = cur;
                  retired_head.compare_exchange_strong(
                      expected_head, next,
                      std::memory_order_acq_rel,
                      std::memory_order_relaxed);
              }
              delete static_cast<std::vector<std::mutex>*>(cur->ptr);
              auto* tmp = cur;
              cur = cur->next;
              delete tmp;
          } else {
              prev = cur;
              cur = cur->next;
          }
      }
  }

  // Waits until all bucket locks are observed free before resizing proceeds.
  void Quiesce() {
    auto curLocks = locks_.load(std::memory_order_acquire);

    for (std::mutex& lock : *curLocks) {
      // attempt to lock
      while (!lock.try_lock()){ 
        // fail: yield to let the thread with lock complete
        std::this_thread::yield();
      }
      // succeed: release lock
      lock.unlock();
    }
  }

};

#endif  // HASH_SET_REFINABLE_H
