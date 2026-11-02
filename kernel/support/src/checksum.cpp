#include "pixcake/support/checksum.hpp"

namespace pixcake {

std::uint64_t checksum_fnv1a64(const std::uint8_t* data, std::size_t size) {
  std::uint64_t hash = 14695981039346656037ULL;
  constexpr std::uint64_t kPrime = 1099511628211ULL;
  for (std::size_t i = 0; i < size; ++i) {
    hash ^= data[i];
    hash *= kPrime;
  }
  return hash;
}

}  // namespace pixcake
