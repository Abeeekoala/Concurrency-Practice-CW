#ifndef HASH_SET_REFINABLE_H
#define HASH_SET_REFINABLE_H

#include <cassert>

#include "src/hash_set_base.h"

template <typename T>
class HashSetRefinable : public HashSetBase<T> {
 public:
  explicit HashSetRefinable(size_t /*initial_capacity*/) {}

  bool Add(T /*elem*/) final {
    assert(false && "Not implemented yet");
    return false;
  }

  bool Remove(T /*elem*/) final {
    assert(false && "Not implemented yet");
    return false;
  }

  [[nodiscard]] bool Contains(T /*elem*/) final {
    assert(false && "Not implemented yet");
    return false;
  }

  [[nodiscard]] size_t Size() const final {
    assert(false && "Not implemented yet");
    return 0u;
  }
};

#endif  // HASH_SET_REFINABLE_H
