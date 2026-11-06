# 内核写入边界收口验收记录：2026-11-05

## 范围

本次只处理现有内核的持久化写入边界，展示线没有纳入。导入、真实解码、渲染、AI、真实登录、云服务、完整导出进程、IPC 和 `kernel-cli` 仍按复审记录标为未实现。

## 已完成

- `WriteScope` 在 `pixcake_persist` 中提供同一路径递归互斥；POSIX 文件系统对 `<path>.lock` 使用非阻塞 `flock`，不同进程不能同时提交同一路径。
- `CommandGate` 注册极速导出门闩。文档、图层、预设、同步、粘贴和原子文件提交都经过同一写区；直接调用文档提交也能观察到已注册的门闩。
- 参数范围改为 `CapabilityRegistry` 的 `ParameterMode` 唯一来源，覆盖合同明确的白平衡、影调、AI 色彩程度、蒙版羽化、裁剪角度、眼神光位置/旋转和光影均匀明度。未知参数仍不能进入正式提交。
- 源文件删除加入“删除待完成”状态。删除意图先落库，磁盘删除使用已登记文件身份；权限失败、磁盘失败或索引更新失败后，启动 presence 扫描可以收敛到可编辑或原文件缺失。
- 保留未知和过新文档/图层片段，写入失败时不推进内存修订、不替换正式文件。

## 验证

```text
cmake -S . -B /private/tmp/pixcake-kernel-20261105 -DCMAKE_BUILD_TYPE=Debug
cmake --build /private/tmp/pixcake-kernel-20261105 --parallel 8
ctest --test-dir /private/tmp/pixcake-kernel-20261105 --output-on-failure
git diff --check
```

结果：**29/29 CTest 通过，6.03 秒；架构门禁通过；`git diff --check` 通过。** 新增/加强的验证包括 POSIX 双文件系统锁竞争、注册门闩下的直接文档提交、图层保存门闩、参数模式边界，以及删除后最终 presence 写入失败的启动恢复。

## 未纳入本次完成定义

七关复审中仍然没有责任模块的导入验真/解码、渲染进程和 GPU、AI 生成、真实登录会话、云服务、IPC 协议、导出进程和 `kernel-cli`，不会被这份内核写入边界验收记录误标为完成。
