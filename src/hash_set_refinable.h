#ifndef HASH_SET_REFINABLE_H
#define HASH_SET_REFINABLE_H

#include <algorithm>
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
  std::mutex mtx_; // Mutex to protect the list itself
  std::vector<std::atomic<void*>*> all_hps_;

 public:
  // on construction of thread_local HP
  void registerHP(std::atomic<void*>* hp) {
    std::lock_guard<std::mutex> lock(mtx_);
    all_hps_.push_back(hp);
  }

  // on destruction of thread_local HP
  void unregisterHP(std::atomic<void*>* hp) {
    std::lock_guard<std::mutex> lock(mtx_);
    for (size_t i = 0; i < all_hps_.size(); ++i) {
      if (all_hps_[i] == hp) {
        // Simple swap-and-pop to remove
        std::swap(all_hps_[i], all_hps_.back());
        all_hps_.pop_back();
        return;
      }
    }
  }

  // for resize() to get a list of all active pointers
  std::vector<void*> getAllHazardousPtrs() {
    std::vector<void*> hazardous;
    std::lock_guard<std::mutex> lock(mtx_);
    
    hazardous.reserve(all_hps_.size());
    for (auto* hp_slot : all_hps_) {
      void* ptr = hp_slot->load(std::memory_order_acquire);
      if (ptr != nullptr) {
        hazardous.push_back(ptr);
      }
    }
    return hazardous;
  }
};

// thread_local Hazard Pointer
class HazardPointer {
  HPRegistry& registry_;
  std::atomic<void*> hp_{nullptr};

 public:
  HazardPointer(HPRegistry& registry) : registry_(registry) {
    registry_.registerHP(&hp_);
  }
  ~HazardPointer() {
    registry_.unregisterHP(&hp_);
  }
  
  // Make HazardPointer non-copyable/movable
  HazardPointer(const HazardPointer&) = delete;
  HazardPointer& operator=(const HazardPointer&) = delete;

  void set(void* ptr) {
    hp_.store(ptr, std::memory_order_release);
  }
  void clear() {
    hp_.store(nullptr, std::memory_order_release);
  }
};

template <typename T>
class HashSetRefinable : public HashSetBase<T> {
 public:
  explicit HashSetRefinable(size_t initial_capacity) {

    table_.store(new std::vector<std::vector<T>>(initial_capacity), std::memory_order_relaxed);
    locks_.store(new std::vector<std::mutex>(initial_capacity), std::memory_order_relaxed);
    
    size_.store(0);
    owner_.store(pack(nullptr, false));

  }
  
  // Desctuctor: delete raw pointers
  ~HashSetRefinable() override {
    delete table_.load();
    delete locks_.load();
  }

  bool Add(T elem) final {
    std::unique_lock<std::mutex> lock = acquire(elem);

    auto curTable = table_.load(std::memory_order_relaxed);
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

    auto curTable = table_.load(std::memory_order_relaxed);
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

    auto curTable = table_.load(std::memory_order_relaxed);
    size_t index = BucketIndex(elem, curTable->size());
    auto& bucket = (*curTable)[index];

    return std::find(bucket.begin(), bucket.end(), elem) != bucket.end();
  }

 [[nodiscard]] size_t Size() const final {
    return size_.load();
  }
 private:
  // Accessors for shared hazard pointer state. We intentionally leak these
  // allocations so the runtime never runs their destructors at shutdown.
  static HPRegistry& registry() {
    static HPRegistry* global_registry = new HPRegistry();
    return *global_registry;
  }

  static std::mutex& garbageMutex() {
    static std::mutex* mtx = new std::mutex();
    return *mtx;
  }

  static std::vector<std::vector<std::mutex>*>& garbageList() {
    static auto* list = new std::vector<std::vector<std::mutex>*>();
    return *list;
  }
  
  // --- Per-thread HP instance ---
  static HazardPointer& getThreadHP() {
      // This is initialized once per thread, on its first call
      static thread_local HazardPointer* my_hp = new HazardPointer(registry());
      return *my_hp;
  }

  // Location will be unique for each thread
  inline static thread_local int dummy_thread_id_{};

  std::atomic<std::vector<std::vector<T>>*> table_{nullptr};
  std::atomic<std::vector<std::mutex>*> locks_{nullptr};

  std::atomic<std::size_t> size_{0};
  
  // Bit stealing:
  // pack bool:mark with pointer to thread to achieve lock free atomic
  // pointer usually ends with 2 (or 3) bytes of zeros,
  // depending on the memory alignment,
  // so the last bit can be used for mark -> 0x... 1000, 1 => 0x... 1001
  std::atomic<uintptr_t> owner_{0};

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
    HazardPointer& hp = getThreadHP();

    while (true) {
      // 1. Check for resize (fast spin, no RMW)
      do {
        uintptr_t packed = owner_.load(std::memory_order_acquire);
        if (getMark(packed) && getPointer(packed) != getThreadToken()) {
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

      if ((!isMarked || curOwner == getThreadToken()) &&
            locks_.load(std::memory_order_acquire) == curLocks){
        hp.clear();
        return lock;
      }
      // Retry until succeed
      hp.clear();
    }
  }
  
  bool policy() const { 
    auto curTable = table_.load(std::memory_order_relaxed);  
    return size_.load() / curTable->size() > 4; 
  }
  
  void resize() {
    // claim ownership
    void* me = getThreadToken();
    uintptr_t expected = pack(nullptr, false);
    uintptr_t desire = pack(me, true);

    //successful exchange needs to be visible to all threads -> acq_rel; failure at most acquire
    if (!owner_.compare_exchange_strong(expected, desire, std::memory_order_acq_rel, std::memory_order_acquire)) {
      // other thread is resizing
      return;
    }

    auto old_table_ptr = table_.load(std::memory_order_relaxed);
    auto old_locks_ptr = locks_.load(std::memory_order_acquire);

    size_t old_capacity = old_table_ptr->size();
    
    // check if other already resized
    auto latest = table_.load(std::memory_order_relaxed);
    if (latest->size() != old_capacity) {
      owner_.store(pack(nullptr, false), std::memory_order_release);
      return;
    }
    
    // wait for all threads complete and release locks
    quiesce();

    size_t new_capacity = old_capacity * 2;
    // new pointer for resized vectors
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
    owner_.store(pack(nullptr, false), std::memory_order_release);

    reclaim(old_locks_ptr);
  }

  void reclaim(std::vector<std::mutex>* old_locks_ptr) {
    // 1. Add our pointer to the global garbage list
    {
      std::lock_guard<std::mutex> lock(garbageMutex());
      garbageList().push_back(old_locks_ptr);
    }
    
    // 2. Get a snapshot of all currently hazardous pointers
    std::vector<void*> hazardous_ptrs = registry().getAllHazardousPtrs();

    auto isHazardous = [&](void* candidate) {
      for (void* hp : hazardous_ptrs) {
        if (hp == candidate) {
          return true;
        }
      }
      return false;
    };

    // 3. Try to clean up the *global* list
    {
      std::lock_guard<std::mutex> lock(garbageMutex());
      auto& garbage = garbageList();
      garbage.erase(
        std::remove_if(garbage.begin(), garbage.end(),
          [&](std::vector<std::mutex>* ptr) {
            if (isHazardous(ptr)) {
              return false; // keep it
            }
            delete ptr;
            return true;
          }),
        garbage.end());
    }
  }

  void quiesce() {
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

  size_t BucketIndex(const T& elem, const size_t capacity) const {
    return std::hash<T>()(elem) % capacity;
  }

  size_t MutexIndex(const T& elem, const size_t lock_count) const {
    return std::hash<T>()(elem) % lock_count;
  }

};

#endif  // HASH_SET_REFINABLE_H
