#include "src/hash_set_coarse_grained.h"
#include "src/hash_set_refinable.h"
#include "src/hash_set_sequential.h"
#include "src/hash_set_striped.h"

namespace check_all {

void Placeholder();

void Placeholder() {
  {
    HashSetCoarseGrained<int> hs(16);
    hs.Add(1);
    hs.Remove(1);
    (void)hs.Size();
    (void)hs.Contains(1);
  }

  {
    HashSetRefinable<int> hs(16);
    hs.Add(1);
    hs.Remove(1);
    (void)hs.Size();
    (void)hs.Contains(1);
  }

  {
    HashSetSequential<int> hs(16);
    hs.Add(1);
    hs.Remove(1);
    (void)hs.Size();
    (void)hs.Contains(1);
  }

  {
    HashSetStriped<int> hs(16);
    hs.Add(1);
    hs.Remove(1);
    (void)hs.Size();
    (void)hs.Contains(1);
  }
}

}  // namespace check_all
