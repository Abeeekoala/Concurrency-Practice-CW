#include "src/hash_set_sequential.h"

namespace check_sequential {

void Placeholder();

void Placeholder() {
  HashSetSequential<int> hs(16);
  hs.Add(1);
  hs.Remove(1);
  (void)hs.Size();
  (void)hs.Contains(1);
}

}  // namespace check_sequential
