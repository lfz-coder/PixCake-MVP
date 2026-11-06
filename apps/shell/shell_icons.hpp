#pragma once

#include <QString>

#include <string_view>

namespace pixcake::shell {

// 图标按文案表的“组/条目编号”查（ui/shell/page_copy），不按显示文字查。
QString iconPath(std::string_view list, std::string_view id);

}  // namespace pixcake::shell
