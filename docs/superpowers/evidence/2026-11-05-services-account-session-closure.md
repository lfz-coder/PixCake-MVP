# Services Account 本机会话边界闭环证据

日期：2026-11-05

## 对应计划

`docs/superpowers/plans/2026-11-05-services-account-session-closure.md`

## 已完成内容

- `pixcake_account` 保存手机号短信、微信和商家账号三种登录方式，以及账号、商家、会话句柄、订阅和布局快照。
- 剩余张数与创意点数保持独立非负字段；坏缓存返回状态冲突并保留当前有效快照，不把余额降成零。
- 无缓存保持未登录；有效缓存可恢复会话；退出登录只清账号状态，不触碰项目库或本地项目文件。
- 自定义工具栏最多 5 个、收藏色最多 6 个；超限请求整体拒绝，布局数据不进入精修文档。
- 模块只依赖 `pixcake_support`，不依赖 Qt、OpenGL、网络、项目库或账本实现。

## 验证

```text
cmake -S . -B /private/tmp/pixcake-services-account-20261105 -DCMAKE_BUILD_TYPE=Debug
cmake --build /private/tmp/pixcake-services-account-20261105 -j2
ctest --test-dir /private/tmp/pixcake-services-account-20261105 --output-on-failure
git diff --check
```

结果：**47/47 CTest 通过**，包括 `test_account_session`、`test_renderd`、`test_exportd`、
`arch_dependencies` 和 `capability_ownership`；`git diff --check` 通过。

## 未覆盖范围

短信/微信/商家网络认证、账号服务 HTTP 协议、支付、余额扣费、项目库、远端同步和 UI
登录页面仍未实现。
