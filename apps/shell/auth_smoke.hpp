#pragma once
namespace pixcake::shell {
class ShellWindow;
#ifdef PIXCAKE_DEV_PAGES
bool smoke_auth_flow(ShellWindow& window);
#endif
}
