#ifndef HASH_SET_REFINABLE_H
#define HASH_SET_REFINABLE_H

#include <cassert>
#include <functional>
#include <mutex>
#include <atomic>
#include <thread>

#include "src/hash_set_base.h"
template <typename T>
class HashSetRefinable : public HashSetBase<T> {
 public:
  explicit HashSetRefinable(size_t initial_capacity) {

    table_ = std::make_shared<std::vector<std::vector<T>>>(initial_capacity);
    locks_ = std::make_shared<std::vector<std::mutex>>(initial_capacity);
    
    size_.store(0);
    owner_.store(pack(nullptr, false));

  }

  bool Add(T elem) final {
    std::unique_lock<std::mutex> lock = acquire(elem);

    auto curTable = std::atomic_load_explicit(&table_, std::memory_order_acquire);
    size_t index = BucketIndex(elem, curTable->size());
    auto& bucket = (*curTable)[index];

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
    std::unique_lock<std::mutex> lock = acquire(elem);

    auto curTable = std::atomic_load_explicit(&table_, std::memory_order_acquire);
    size_t index = BucketIndex(elem, curTable->size());
    auto& bucket = (*curTable)[index];

    auto it = std::find(bucket.begin(), bucket.end(), elem);
    if (it == bucket.end()) {
      return false;  // Element not found
    }

    bucket.erase(it);
    size_.fetch_sub(1);
    return true;
  }

  [[nodiscard]] bool Contains(T elem) final {
    std::unique_lock<std::mutex> lock = acquire(elem);

    auto curTable = std::atomic_load_explicit(&table_, std::memory_order_acquire);
    size_t index = BucketIndex(elem, curTable->size());
    auto& bucket = (*curTable)[index];

    return std::find(bucket.begin(), bucket.end(), elem) != bucket.end();
  }

  [[nodiscard]] size_t Size() const final {
    return size_.load();
  }
 private:
  // Location will be unique for each thread
  inline static thread_local int dummy_thread_id_{};

  std::shared_ptr<std::vector<std::vector<T>>> table_;
  std::shared_ptr<std::vector<std::mutex>> locks_;

  std::atomic<std::size_t> size_;
  
  // Bit stealing:
  // pack bool:mark with pointer to thread to achieve lock free atomic
  // pointer usually ends with 2 (or 3) bytes of zeros,
  // depending on the memory alignment,
  // so the last bit can be used for mark -> 0x... 1000, 1 => 0x... 1001
  std::atomic<uintptr_t> owner_;

  // Helper functions for owner field
  uintptr_t pack(void* ptr, bool mark) {
    return reinterpret_cast<uintptr_t>(ptr) | (mark ? 1 : 0);
  }

  void* getPointer(uintptr_t packedValue) {
    // clear the mark for memory alignment
    return reinterpret_cast<void*>(packedValue & ~static_cast<uintptr_t>(1));
  }

  bool getMark(uintptr_t packedValue) {
    // return mark (LSB)
    return (packedValue & 1) == 1;
  }

  //Helper function to get thread token
  void* getThreadToken() {
    return &dummy_thread_id_;
  }

  std::unique_lock<std::mutex> acquire(const T& elem) {
    while (true) {
      void* curOwner = nullptr;
      bool isMarked = false;

      // Spin till no resize
      do {
        uintptr_t packed = owner_.load(std::memory_order_acquire);
        curOwner = getPointer(packed);
        isMarked = getMark(packed);
      } while (isMarked && curOwner != getThreadToken());
      
      // Get locks
      auto curLocks = std::atomic_load_explicit(&locks_, std::memory_order_acquire);
      std::mutex& m = (*curLocks)[MutexIndex(elem, curLocks->size())];
      std::unique_lock<std::mutex> lock(m);

      // Second check (no resizing) and (lock_ array is the same)
      uintptr_t packed = owner_.load(std::memory_order_acquire);
      curOwner = getPointer(packed);
      isMarked = getMark(packed);
      if ((!isMarked || curOwner == getThreadToken()) && std::atomic_load_explicit(&locks_, std::memory_order_acquire) == curLocks){
        return lock;
      }
      // Retry until succeed
    }
  }
  
  bool policy() const { 
    auto curTable = std::atomic_load_explicit(&table_, std::memory_order_acquire);  
    return size_.load() / curTable->size() > 4; 
  }
  
  void resize() {
    auto curTable = std::atomic_load_explicit(&table_, std::memory_order_acquire);

    size_t old_capacity = curTable->size();
    size_t new_capacity = old_capacity * 2;

    // claim ownership
    void* me = getThreadToken();
    uintptr_t expected = pack(nullptr, false);
    uintptr_t desire = pack(me, true);

    //successful exchange needs to be visible to all threads -> acq_rel; failure at most acquire
    if (!owner_.compare_exchange_strong(expected, desire, std::memory_order_acq_rel, std::memory_order_acquire)) {
      // other thread is resizing
      return;
    }

    // check if other already resized
    auto latest = std::atomic_load_explicit(&table_, std::memory_order_acquire);
    if (latest->size() != old_capacity) {
      owner_.store(expected);
      return;
    }
    
    // wait for all threads complete and release locks
    quiesce();

    // new pointer for resized vectors
    auto new_table_ptr = 
        std::make_shared<std::vector<std::vector<T>>>(new_capacity);
    auto new_locks_ptr = 
        std::make_shared<std::vector<std::mutex>>(new_capacity);
    
    // Re-hash elements 
    for (const auto& bucket : *table_) {
        for (const T& elem : bucket) {
            size_t index = BucketIndex(elem, new_capacity); 
            (*new_table_ptr)[index].push_back(elem);
        }
    }

    std::atomic_store_explicit(&table_, new_table_ptr, std::memory_order_release);
    std::atomic_store_explicit(&locks_, new_locks_ptr, std::memory_order_release);

    
    // return ownership
    owner_.store(expected);
  }

  void quiesce() {
    auto curLocks = std::atomic_load_explicit(&locks_, std::memory_order_acquire);

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

  size_t BucketIndex(const T& elem, const size_t capacity) const {
    return std::hash<T>()(elem) % capacity;
  }

  size_t MutexIndex(const T& elem, const size_t lock_count) const {
    return std::hash<T>()(elem) % lock_count;
  }

};

#endif  // HASH_SET_REFINABLE_H
