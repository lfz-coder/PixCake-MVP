#pragma once

#include "pixcake/support/result_code.hpp"

#include <string>

namespace pixcake {

// 用户能看见的失败（运维清单第 3 节）：结果码按场景换成用户读得懂的原因。只写日志、显示空白或
// “出错了”都不行。不同结果码的文案不混用（O-16）。
enum class NoticeScene { Login, Import, OpenDocument, Edit, Export, Generation };

struct UserNotice {
  std::string text;             // 成功时为空
  bool stays_on_page = true;    // 失败后留在当前页面，已有数据不变
  bool retryable = false;       // 可以按规则重试（断网、磁盘满等）
};

UserNotice notice_for(NoticeScene scene, ResultCode code);

}  // namespace pixcake
