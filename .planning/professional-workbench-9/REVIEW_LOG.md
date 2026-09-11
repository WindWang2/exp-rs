# REVIEW LOG — professional-workbench-9

## Round 0 — baseline 自审（2026-09-12）

主 Agent 对最新 master 的 UI 安全面做了定点审计，发现（详见 ISSUE_TRIAGE.md）：

| ID | 优先级 | 发现 | 处置 |
|---|---|---|---|
| F1 | P1 | project.new/open/save 菜单裸 action 与 CommandRegistry 双重持有 Ctrl+N/O/S（复查后实际范围更大：ZoomIn/Out/Quit/Ctrl+E/Ctrl+Shift+F/Ctrl+L/H/Ctrl+Shift+I/D/A/C/S/F5 共 19 项） | M2 修复（63d69a60ee） |
| F2 | P1 | pipeline_editor_dock tooltip 声称 Ctrl+N/O/S 但 action 无绑定（#882 残留） | M2 修复 |
| F3 | P2 | histogram_widget GDALOpen 失败静默，无 UI 错误呈现 | M0 修复（c2951a4570） |
| F4 | P1 | roi_statistics_widget::m_requestEpoch 非 atomic，被 pool worker 线程读取（数据竞争 / UB） | M0 修复（std::atomic<uint64_t>） |
| F5 | P1 | test_shortcut_conflicts 只做文件内重复扫描，跨文件（menus vs registry）双权威恰为盲区 | M2 新增 cross-source union gate |
| F6 | P2 | main/build 之前所有 worktree 都假设 shortcut 唯一性由"注册期拒绝"保障，但无人测 installShortcut 的 second-owner 路径 | 由既有 registry 测试 + 新 gate 覆盖（接受） |

## Round 1 — milestone 自审（2026-09-12，实现后逐项复核）

| ID | 优先级 | 发现 | 处置 |
|---|---|---|---|
| R1-1 | P1 | WorkbenchStateModel 首版连接了不存在的 `QgsMapCanvas::mapToolChanged`（fork 用 QGIS 3.x 名 `mapToolSet`） | 编译期暴露，已修 |
| R1-2 | P2 | test_scan_pool 初版 rendezvous 用自旋等待自身完成（死锁缺陷），改为信号量确定性握手 | 已修 |
| R1-3 | P2 | QPointer 在 worker 线程 .data() 与 GUI 线程析构并发，理论上非线程安全（本仓库既有一贯模式） | **有意接受**：对齐指针尺寸的读写在 x86/ARM 上无实际撕裂；彻底方案（weak_ptr 握手）涉及全部 scan 用户，记录为 follow-up，不在本轮大规模翻改 |
| R1-4 | P3 | m_requestEpoch 读比较在 marshaled lambda（GUI 线程）与 worker（原子读）都做，双检查冗余但无害 | 接受（防御性） |
| R1-5 | P2 | M7 分页翻页会 full refresh()（重建整棵树）——200k 索引的 refresh 已被 8.0 的 coalesced timer + light index 保证 <2s 上界；分页未引入新的 O(N²)（切片为 O(window)） | 接受；PERFORMANCE.md 记录上界 |
| R1-6 | P2 | registerPluginCommands 的命令 id 用索引（plugin.<id>.<n>），reload 后 n 可能漂移 | 接受：命令生命周期跟随渲染 action（availability 守卫），palette 展示以 title 为准；稳定 id 需 schema 增加 contribution id 字段，记 follow-up |
| R1-7 | P1 | GDAL 3.13.3 升级使 master 的 canonical_metadata.cpp 无法编译（环境破坏，非本方向引入） | 一行 seam 修复（a6b349d69d），与本文件内既有 size_t 拼写模式一致 |

## Final — adversarial review（subagent ≤2）

（见下方追加记录；P0/P1 全修，P2 原则全修，P3 修或逐条接受。）
