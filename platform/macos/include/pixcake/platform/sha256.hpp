#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
namespace pixcake::platform {
std::string sha256(const std::uint8_t* bytes, std::size_t size);
std::string random_identifier();
}
