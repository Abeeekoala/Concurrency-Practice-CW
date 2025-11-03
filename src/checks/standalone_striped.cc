#include "src/hash_set_striped.h"

namespace check_striped {

void Placeholder();

void Placeholder() {
  HashSetStriped<int> hs(16);
  hs.Add(1);
  hs.Remove(1);
  (void)hs.Size();
  (void)hs.Contains(1);
}

}  // namespace check_striped
