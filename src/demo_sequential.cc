#include <iostream>

#include "src/hash_set_sequential.h"

int main(int argc, char** argv) {
  if (argc != 3) {
    std::cerr << "Usage: " << argv[0] << " initial_capacity count" << std::endl;
    return 1;
  }
  size_t initial_capacity = std::stoul(std::string(argv[1]));
  size_t count = std::stoul(std::string(argv[2]));

  HashSetSequential<int> sequential_set(initial_capacity);

  for (size_t i = 0; i < count; i++) {
    sequential_set.Add(static_cast<int>(i));
  }
  if (sequential_set.Size() != count) {
    std::cerr << "Expected size " << count << ", got " << sequential_set.Size()
              << std::endl;
    return 1;
  }
  for (size_t i = 0; i < count; i++) {
    if (sequential_set.Size() != count - i) {
      std::cerr << "Expected size " << (count - i) << ", got "
                << sequential_set.Size() << std::endl;
    }
    int expected_value = static_cast<int>(i);
    if (!sequential_set.Contains(expected_value)) {
      std::cerr << "Expected value " << expected_value << std::endl;
      return 1;
    }
    sequential_set.Remove(expected_value);
  }
  if (sequential_set.Size() != 0) {
    std::cerr << "Expected empty set, got set with size "
              << sequential_set.Size() << std::endl;
    return 1;
  }

  std::cout << "Sequential hash set tests succeeded" << std::endl;

  return 0;
}
