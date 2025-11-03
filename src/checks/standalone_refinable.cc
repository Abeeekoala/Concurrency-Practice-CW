#include "src/hash_set_refinable.h"

namespace check_refinable {

void Placeholder();

void Placeholder() {
  HashSetRefinable<int> hs(16);
  hs.Add(1);
  hs.Remove(1);
  (void)hs.Size();
  (void)hs.Contains(1);
}

}  // namespace check_refinable
