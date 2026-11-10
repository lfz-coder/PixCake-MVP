#include "pixcake/platform/sha256.hpp"
#include <CommonCrypto/CommonDigest.h>
#include <Security/SecRandom.h>
#include <limits>
namespace pixcake::platform {
std::string random_identifier() {
  unsigned char bytes[16];
  if (SecRandomCopyBytes(kSecRandomDefault, sizeof(bytes), bytes) != errSecSuccess) return {};
  constexpr char hex[] = "0123456789abcdef";
  std::string result;
  for (auto value : bytes) { result.push_back(hex[value >> 4]); result.push_back(hex[value & 15]); }
  return result;
}
std::string sha256(const std::uint8_t* bytes, std::size_t size) {
  if (size > std::numeric_limits<CC_LONG>::max() || (!bytes && size)) return {};
  unsigned char digest[CC_SHA256_DIGEST_LENGTH];
  if (!CC_SHA256(bytes, static_cast<CC_LONG>(size), digest)) return {};
  constexpr char hex[] = "0123456789abcdef";
  std::string result; result.reserve(64);
  for (auto value : digest) { result.push_back(hex[value >> 4]); result.push_back(hex[value & 15]); }
  return result;
}
}
