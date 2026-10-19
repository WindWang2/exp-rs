# R18-D DECISIONS

本文件记录本轨执行中的权衡与默认选择（autonomy=full，不向用户反复申请常规设计决定）。

1. **技能路由**：宿主无 `/goal-loop`；已按 README 从 bravesd/goal-loop 安装到 `~/.zcode/skills/` 并由用户显式 `/goal-loop` 触发。Oracle 协议以任务书 §0-8 为主，goal-loop 的账本纪律为辅。全局轨道进度不写仓库根 `.goal-loop-ledger.md`（任务书明确禁止），只写本目录 LOOP_LEDGER.md。
2. **基线**：实时 recon 结果 `a60c8831c`（#1513 已合并），不用快照 SHA。
3. **构建并发**：取 `min(4, 仓库上限2, RAM允许) = -j2`；所有 cmake configure/build/ctest 均置于 `/tmp/exp-rs-five-tracks-build.lock` 全局 flock + resource_guard 下。不改资源保护脚本。
4. **工作包选择**：从两份只读审计（workbench 链 + mapspec/cartography 链）的 16 个候选中按 `影响×可测性÷风险` 排序，优先选有明确契约先例（同文件已修过同类问题）的项；每轮一个聚焦修改，RED 先行。
5. **W1（属性表/QgisApp 门面绕过 Edit Lease，审计 P0）**：涉及 QGIS vendored 门面与双编辑权威收敛，风险大；本轮先做其可独立验证的子集（shell 侧 editingStarted/editingStopped 监听与状态同步），完整 lease 路由若时间允许再做，否则写入 CROSS_TRACK_FINDINGS/遗留。
6. **视觉 goldens**：不整批重录；只有修复确需更新 golden 时逐个审查式更新并在 PR 标出。
7. **共享文件**：默认只读；本轨只扩充已注册测试文件与自有源文件；tests/CMakeLists.txt 仅在新增测试文件时加最小注册块（若可避免则不新增文件，直接在既有测试文件加 SECTION）。
8. **修复风格**：surgical patch，先读后写；每个 WP 一个原子 commit；commit message 引用 RED 证据。
9. **跨轨发现**：只记录 CROSS_TRACK_FINDINGS.md，不顺手修。
10. **NOT_EXECUTED 纪律**：headful/手动视觉路径若无环境，明确标注，不做伪 GREEN。
