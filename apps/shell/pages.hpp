#pragma once

#include "page_kit.hpp"

namespace pixcake::shell {

// 每页一个文件（login_page.cpp 等），只画控件。
QWidget* buildLoginPage(const PageContext& context);
QWidget* buildHomePage(const PageContext& context);
QWidget* buildLibraryPage(const PageContext& context);
void prepareRefinePageEntry();
QWidget* buildRefinePage(const PageContext& context);
QWidget* buildCreativePage(const PageContext& context);

}  // namespace pixcake::shell
