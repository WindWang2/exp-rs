# ZCode R20-04 REVIEW_R1（独立 fresh reviewer，2026-10-23）

Reviewer：独立 general-purpose 子代理（非实现作者），full diff `origin/master...HEAD`（b7a04d7465 时点）。
裁定：**APPROVE-WITH-FIXES**。

## 发现与处置

| # | 级别 | 位置 | 发现 | 处置 |
|---|---|---|---|---|
| 1 | P2 | test_raster_processing_dialog_base.cpp cancel SECTION | 取消点击若与任务自然完成竞态，空结果走 ComputationError → onFailed → 模态 QMessageBox 卡死 drain 循环（同文件 failure 测试有 modalTimer 守卫，本节缺失） | ✅ 已修：加入同款 modalTimer 守卫 |
| 2 | P2→收紧 | 同上 :166 | `steps < kTotalSteps` 单向弱界（299/300 也过）；收紧为 `< kTotalSteps/2` 更证"及时"（取消点击时 steps≈3-10，GREEN 余量巨大；RED 时 ==300 仍必红） | ✅ 已修：收紧到 kTotalSteps/2 |
| 3 | P3 | raster_processing_dialog_base.cpp gdalCancelProgress | probe 为 public std::function<bool()>，未来可抛异常的目标会穿越 GDAL C 栈帧 → UB/terminate | ✅ 已修：try/catch 包裹，抛异常探针按"继续"处理 |
| 4 | P3 | raster_processing_dialog_base.h 探针文档 | const& 探针传给 GDAL void* 需局部拷贝，文档示例误导 | ✅ 已修：头文件示例改为 local copy 模式 |
| 5a | P3 | layout_tools.cpp:784 layout:export dpi schema 无 bounds 描述 | — | ⛔ 评估后不改：加 description 会漂移快照钉定的 inputSchema（逐字比较），需 regen 且波及正在构建 snapshot 测试的兄弟轨；以注释说明 + 运行时错误承载边界。记录于代码注释。 |
| 5b | P3 | src/app/layout/qgslayoutdesignerdialog.cpp:129 GUI 导出对话框 min=36 | 第三条 seam（GUI）仍接受 <72 | ✅ 已修：min 36→72（src/app 允许区；无测试钉 36） |
| 6 | P3 | cancel SECTION resultAnnounced 断言恒真空转 | auto-accept 默认 true 时 resultReadyForDisplay 永不发射 | ✅ 已修：本节 setShouldAutoAcceptOnSuccess(false)，断言变为真实oracle；恢复段改用 resultAnnounced 为成功面 |
| 7 | P3 | 范围诚实 | runGdalTask 无生产调用者（thin-client guardrail 保持关闭）；修复固化 seam 契约 + 测试 + 可复用 GDAL 适配器，不改变现有对话框用户可见行为（它们走 runOperatorTask 协作取消） | 📝 记录进 PR 正文（scope honesty），不改码 |

## Reviewer 独立验证的干净项（摘）

- CT-6 完备性：122 个 name() 字面量全部钉于快照；36 个可解释 inputSchema 体逐值比对零 diff；helper 型（pathSchema/schemaForObject/objectSchema/AgentTool 规范化）皆 canonical —— 无第 8 个会让 registry 腿失败的工具；快照无需 regen。
- 探针线程安全/生命周期：ctx 活动于 worker 栈帧、纯原子读、executor 无 this 捕获、GUI/worker 两线程路径（排队取消、终态）覆盖。
- 签名爆破面：树内零调用者破坏；guardrail 测试不受影响。
- DPI：无任何调用者/测试/文档依赖 dpi<72 或旧错误文案（repo 级 grep 含 pi/ 与 docs/）；NaN 守卫形式保留；72 边界含入测试。
- 范围泄漏：无 —— diff 限于声明文件。

## R1 修复提交

`edc7c27e17 (findings 1/2/3/4/5b/6)`（findings 1/2/3/4/5b/6）。
