#pragma once

#include <cstddef>
#include <cstdint>

namespace pixcake {

std::uint64_t checksum_fnv1a64(const std::uint8_t* data, std::size_t size);

}  // namespace pixcake
