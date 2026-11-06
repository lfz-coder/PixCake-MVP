#include "pixcake/support/user_notice.hpp"

namespace pixcake {
namespace {

UserNotice make(std::string text, bool retryable = false) {
  UserNotice notice;
  notice.text = std::move(text);
  notice.retryable = retryable;
  return notice;
}

}  // namespace

UserNotice notice_for(NoticeScene scene, ResultCode code) {
  switch (code) {
    case ResultCode::Ok: {
      UserNotice ok;
      ok.stays_on_page = false;
      return ok;
    }
    case ResultCode::UnsupportedFormat:
      return make(scene == NoticeScene::Import ? "格式不支持" : "这种文件格式不支持");
    case ResultCode::ImageCorrupt:
      return make(scene == NoticeScene::Import ? "图片损坏，无法打开" : "图片内容损坏，无法打开");
    case ResultCode::PermissionDenied:
      return make(scene == NoticeScene::Import ? "没有读取权限" : "系统拒绝访问这个文件，请检查权限");
    case ResultCode::DiskFull:
      return make("磁盘空间不足，这次没有写入，已有内容保持不变", true);
    case ResultCode::SourceMissing:
      return make(scene == NoticeScene::Import ? "文件不存在"
                                               : "原文件缺失：参数仍保留，原图回到原位置后自动恢复");
    case ResultCode::DocumentCorrupt:
      return make("参数文件损坏，已隔离；画面显示的是原图，不是之前的精修效果");
    case ResultCode::CapabilityTooNew:
      return make("部分功能的参数来自更新的版本，需要更新应用后才能编辑");
    case ResultCode::Cancelled:
      return make("已取消");
    case ResultCode::NetworkFailed:
      return make(scene == NoticeScene::Login ? "网络连接失败，请检查网络后重试"
                                              : "网络失败，这次请求没有完成，请检查网络后重试",
                  true);
    case ResultCode::InsufficientBalance:
      return make(scene == NoticeScene::Export ? "张数不足，这次导出没有开始" : "点数或张数不足，这次没有扣费");
    case ResultCode::FastExportBusy:
      return make("极速导出进行中，关闭极速导出后才能继续编辑和预览");
    case ResultCode::StateConflict:
      return make("当前状态不允许这个操作，内容已是最新或正被占用，请刷新后再试");
    case ResultCode::IoFailed:
      return make("读写文件失败，这次没有写入，已有内容保持不变", true);
  }
  return make("当前状态不允许这个操作，内容已是最新或正被占用，请刷新后再试");
}

}  // namespace pixcake
