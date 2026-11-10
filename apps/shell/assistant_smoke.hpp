#pragma once

#include <string>

namespace pixcake::shell {
#ifdef PIXCAKE_DEV_PAGES
bool smoke_assistant_ui();
bool smoke_assistant_generation(const std::string& renderd, const std::string& generated);
#endif
}  // namespace pixcake::shell
