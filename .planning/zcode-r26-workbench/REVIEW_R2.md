# REVIEW R2 — 独立评审（全新上下文，未参与实现，2026-10-31）

判定：R2 发现 1 项 P2 必修 + 10 项 P3；已全部处理。R2 评审员本机实跑 6 新目标 + 5 既有目标（均通过）作为验证基线。

| ID | 严重度 | 内容 | 处理 |
|---|---|---|---|
| P2-1 | P2 | guided cancel 分支未 insert m_failedSteps，取消后连点 Next 仍会 "Experiment complete!" | ✅ 已修：cancel 分支同样 insert（被取消=未完成）；新增 RED 测试 case（3 用例 35 断言） |
| P3-A | P3 | 合并时样本层重建两次（commit 触发 editingStopped 重建 + 显式重建）；startEditing 返回值未查 | ✅ 已修：仅 wasEditable 或 commit 失败时显式重建；startEditing 失败即中止并提示 |
| P3-B | P3 | 通道切换后自旋框停留旧量程（touched 未复位） | ✅ 已修：onChannelChanged 复位 m_spinsTouched |
| P3-C | P3 | 空统计点 Export 弹模态框 | 已评估：保留（一次性信息提示，非阻断）；记录为已知取舍 |
| P3-D | P3 | 汇总像素数只取 band 0，band 0 读失败时谎报 "no valid pixels" | ✅ 已修：取各 band 最大值 |
| P3-E | P3 | histogram_stretch_widget.cpp 无用 <cstdio> include（R1 调试残留） | ✅ 已删 |
| P3-F | P3 | dialogs 成功路径等待文件落盘（早于回调投递）可能竞态 | ✅ 已修：改为等待 statusLabel 离开 Running + 补事件泵送 |
| P3-G | P3 | classification waitForTerminalTask 不泵事件 | ✅ 已修：循环内 processEvents |
| P3-H | P3 | guided 测试 pin CWD/env 不还原 | 已评估：当前二进制两用例都 pin，无实测影响；不引入 RAII 复杂度，记录取舍 |
| P3-I | P3 | exp_gui::sampleBandsGdal（无生产实例化的遗留类）未用同一 NoData 纪律 | 不改（无生产调用方，改它属超范围重构）；已在 ORACLES 备注 |
| P3-J | P3 | 注释与代码语义相反；restoreButton 缺上界检查；测试无 TIMEOUT | ✅ 注释已修；restoreButton 已加上界；TIMEOUT 由 CTest 全局兜底（classification 用例自带 20s killSwitch） |

R2 冲突/重叠核查结论：与 #1724-#1728 源码文件零重叠（唯一共享 tests/CMakeLists.txt 为纯尾部追加，hunk `@@ -17276,3 +17276,145 @@`）；与 R25 五 PR 关键词扫描零命中（无重做）。
R1 修复有效性：R2 逐条代码路径追踪 + 实跑验证全部生效（无 P1-1 guard 误触发：setCutoffs 不发 cutoffsChanged）。
