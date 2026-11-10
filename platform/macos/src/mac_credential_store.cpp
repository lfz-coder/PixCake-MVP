#include "pixcake/platform/mac_credential_store.hpp"

#include <Security/Security.h>
#include <CoreFoundation/CoreFoundation.h>

namespace pixcake::platform {
namespace {
CFMutableDictionaryRef query(const std::string& origin) {
  if (origin.empty() || origin.size() > 2048) return nullptr;
  auto key = CFStringCreateWithBytes(nullptr, reinterpret_cast<const UInt8*>(origin.data()),
                                   static_cast<CFIndex>(origin.size()), kCFStringEncodingUTF8, false);
  if (!key) return nullptr;
  auto values = CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
  CFDictionarySetValue(values, kSecClass, kSecClassGenericPassword);
  CFDictionarySetValue(values, kSecAttrService, CFSTR("com.pixcake.desktop.cloud.refresh"));
  CFDictionarySetValue(values, kSecAttrAccount, key);
  CFDictionarySetValue(values, kSecAttrSynchronizable, kCFBooleanFalse);
  CFRelease(key);
  return values;
}
}
ResultCode MacCredentialStore::read(const std::string& origin, std::string& token) {
  token.clear();
  auto values = query(origin);
  if (!values) return ResultCode::StateConflict;
  CFDictionarySetValue(values, kSecReturnData, kCFBooleanTrue);
  CFDictionarySetValue(values, kSecMatchLimit, kSecMatchLimitOne);
  CFTypeRef result = nullptr;
  const auto status = SecItemCopyMatching(values, &result);
  CFRelease(values);
  if (status == errSecItemNotFound) return ResultCode::Ok;
  if (status != errSecSuccess || !result) return ResultCode::IoFailed;
  const bool valid = CFGetTypeID(result) == CFDataGetTypeID() && CFDataGetLength(static_cast<CFDataRef>(result)) == 43;
  if (valid) token.assign(reinterpret_cast<const char*>(CFDataGetBytePtr(static_cast<CFDataRef>(result))), 43);
  CFRelease(result);
  return valid ? ResultCode::Ok : ResultCode::IoFailed;
}
ResultCode MacCredentialStore::write(const std::string& origin, const std::string& token) {
  if (token.size() != 43) return ResultCode::StateConflict;
  auto values = query(origin);
  if (!values) return ResultCode::StateConflict;
  auto data = CFDataCreate(nullptr, reinterpret_cast<const UInt8*>(token.data()), static_cast<CFIndex>(token.size()));
  auto update = CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
  CFDictionarySetValue(update, kSecValueData, data);
  auto status = SecItemUpdate(values, update);
  if (status == errSecItemNotFound) {
    CFDictionarySetValue(values, kSecValueData, data);
    CFDictionarySetValue(values, kSecAttrAccessible, kSecAttrAccessibleWhenUnlockedThisDeviceOnly);
    status = SecItemAdd(values, nullptr);
  }
  CFRelease(update); CFRelease(data); CFRelease(values);
  return status == errSecSuccess ? ResultCode::Ok : ResultCode::IoFailed;
}
ResultCode MacCredentialStore::erase(const std::string& origin) {
  auto values = query(origin);
  if (!values) return ResultCode::StateConflict;
  const auto status = SecItemDelete(values);
  CFRelease(values);
  return status == errSecSuccess || status == errSecItemNotFound ? ResultCode::Ok : ResultCode::IoFailed;
}
}  // namespace pixcake::platform
