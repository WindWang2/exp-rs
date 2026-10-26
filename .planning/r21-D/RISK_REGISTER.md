# R21-D RISK_REGISTER

| # | 风险 | 概率 | 影响 | 缓解 |
|---|---|---|---|---|
| R1 | 五轨并行编译叠加致 OOM/链接失败（R20-04 曾遇 ld 信号 11） | 中 | 构建阻塞 | flock 串行 + -j2 起步；RSS>70% 降 -j1；定向目标优先于全量 |
| R2 | git fetch 网络不稳（已两次 SSL 错误） | 中 | 基线漂移不可见 | gh API 双通道复核 master SHA；push 前再核一次 |
| R3 | W1b 修复触碰 Display Layer 重定位（commit → assetChanged → 同步换层） | 高（若处理不当） | UAF/回归 | 复用 window 已验证的 id 快照+重解析模式；RED 测试先锁行为 |
| R4 | src/gui vendored 弱桩修改破坏与 upstream diff 基线 | 中 | 合并噪声 | 逐项最小 patch + 记录；不确定则只记录不改 |
| R5 | CP11 3/28 离屏红误判为产品缺陷（或反之） | 中 | 错误修复方向 | 同门复跑 + 与 R19-03/R20-04 记录逐位比对；区分环境红 |
| R6 | 兄弟轨在我提交前合并触碰同文件 | 低-中 | 冲突 | push 前重盘点；共享区只 append |
| R7 | .qm install 修改根 CMakeLists 与他轨争抢 | 低 | 冲突 | append-only 单块；提交前 fetch 对账 |
| R8 | 测试环境无显示（offscreen）致 Qt 交互测试伪红 | 中 | 误报 | 沿用 QT_QPA_PLATFORM=offscreen 既有门；双通道（offscreen 为主，显示可用时复核） |
| R9 | 长会话 token/上下文耗尽 | 中 | 中断 | 断点文件（LOOP_LEDGER/TASKS）随里程碑更新；工作单元原子化 |
