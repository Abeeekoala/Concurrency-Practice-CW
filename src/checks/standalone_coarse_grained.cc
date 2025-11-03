#include "src/hash_set_coarse_grained.h"

namespace check_coarse_grained {

void Placeholder();

void Placeholder() {
  HashSetCoarseGrained<int> hs(16);
  hs.Add(1);
  hs.Remove(1);
  (void)hs.Size();
  (void)hs.Contains(1);
}

}  // namespace check_coarse_grained
