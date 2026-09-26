# DECISIONS — Track 9

## D-1：#1334 冲突策略 = 基于 origin/master 开发 + 外科手术式文件隔离（Phase 0 实测后裁定）

实测 #1334（仍 open）与本轨道相交 8 文件（详见 BASELINE.md §3）。逐文件裁定：
- `mcp_server.cpp/h`：本轨道只改**帧解析/错误应答/协议分发**段（onLineRead / handleRequest 的协议分支 / sendError 合同），不触 sandbox、trust-gate、workspace containment。#1334 对这些段的改动集中在安全门与 workspace_containment 抽取——语义不相交，冲突面为同文件不同 hunk，git 可自动合并的概率高。
- `tests/test_surface_protocol.cpp`：本轨道全部用例**追加在文件尾部**，不移动既有行 → 与 #1334 的同文件改动冲突面最小化。
- `tests/CMakeLists.txt`：仅追加新目标块（若需要），既有行不动。
- `surface_registry.cpp/h`、`meta_protocol_tools.cpp`：**零改动**（只读引用）。
- `pi/exp-rs-spatial.ts`：**零改动**（生产逻辑不动；pi 侧断言全部落在 pi/test/）。
若后续 #1334 合并且冲突面实测过大 → 触发预案 rebase（Phase 7 预算）。

## D-2：快照 = 进程内 hermetic projection，而非真实二进制 wire dump

理由：(a) 比对器必须 hermetic 且确定性——真实二进制的 catalog 依赖 QGIS/GUI provider，机器间不稳定；(b) 真实二进制 leg 已由既有 `CLI tools list matches the projection` 用例以 superset 语义覆盖；(c) 进程内 `collectSurfaceTools()` 与 tools/list 是同一函数（mcp_server.cpp:601），快照锁的是"跨提交漂移"与"pi 消费逻辑"。快照带 `schemaVersion` 字段，加载自校验；再生成命令写入 CMake 注释与测试失败消息（防手改快照作弊）。

## D-3：缺 method 请求 -32601 → -32600 是缺陷修复而非行为破坏

JSON-RPC 2.0：method 成员缺失 = Invalid Request（-32600）。现状 mcp_server.cpp 落入未知 method 分支回 -32601。修复影响面：仅"没有 method 成员"的帧；带未知 method 的帧仍 -32601。无客户端合法路径受影响（pi bridge 总是发 method）。按 TDD 先红后绿。

## D-4：WP-A/B 转"固化防回归"的依据（prompt 预案条款）

Phase 0 快照结论（BASELINE.md §5）：进程内三处 diff 已由既有测试约束近零，但 pi 消费端零逐工具锁死、无共享快照。按 prompt第四节"若 Phase 0 快照显示某侧已零漂移，WP-A/B 对应半边转为固化防回归，火力转移到 WP-C/D"，账本记录重分配。这不降低 3.2 门禁：比对器 + 15 用例照常交付。

## D-5：构建环境实测

cmake 需显式 PATH（/tmp/cmake-3.30.5-linux-x86_64/bin）；ninja 用 /home/kevin/pwb-sdks/root/usr/bin/ninja；`CMAKE_PREFIX_PATH=/home/kevin/pwb-sdks/root/usr`、`GSL_ROOT_DIR=/usr`、Qt6 系统级。系统 RAM 64 GB（可用 ~29 GB），-j2 恒定无需降级；机器 load 高（其他轨道并行编译），测试串行 -j1 红线维持。

## D-6：ADR 增补裁定

表面语义澄清（快照锁死程序 + pi 消费合同）按 ADR 0122 程序记录：不改 0122 决策本体，在其 Consequences 后追加实现注记，或新增轻量 ADR——待 WP-E 收口时依实际改动范围裁定（若仅测试机制则记 ADR-0122 注记即可，避免 ADR 膨胀）。

## D-7：账本落点 = `.planning/mcp-surface-r4/LEDGER.md`（非根目录 `.goal-loop-ledger.md`）

实测根目录 `.goal-loop-ledger.md` 为 git tracked（多轨道共享的已提交历史，来自 #1335/#1337/#1338/#1340 等先例）。向 tracked 共享文件追加 Track 9 内容 = 污染他轨道历史 + 超出白名单。按 prompt"gitignored"意图落 `.planning/*`（实测 gitignored；planning 工件 PR 时按 #1340 先例 `git add -f`）。
