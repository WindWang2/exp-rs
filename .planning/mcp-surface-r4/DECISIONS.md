# DECISIONS — Track 9

## D-1：#1334 冲突策略 = 基于 origin/master 开发 + 外科手术式文件隔离（Phase 0 实测后裁定）

实测 #1334（仍 open）与本轨道相交 8 文件（详见 BASELINE.md §3）。逐文件裁定：
- `mcp_server.cpp/h`：本轨道只改**帧解析/错误应答/协议分发**段（onLineRead / handleRequest 的协议分支 / sendError 合同），不触 sandbox、trust-gate、workspace containment。#1334 对这些段的改动集中在安全门与 workspace_containment 抽取——语义不相交，冲突面为同文件不同 hunk，git 可自动合并的概率高。
- `tests/test_surface_protocol.cpp`：本轨道全部用例**追加在文件尾部**，不移动既有行 → 与 #1334 的同文件改动冲突面最小化。
- `tests/CMakeLists.txt`：原计划"仅追加新目标块，既有行不动"。**实测偏差（review P2 #10，已记录）**：master 的 test_surface_parity / test_surface_protocol / surface_mcp_host 三个既有目标在全新 configure 下链接失败（缺 `Sicnu::agent_ops` 链接 → OpsDriver::apply 与 agent_loop VerificationReport::aggregate 未解析；test_mcp_server 有该链接，缺口实锤）——不修则本轨道 Oracle 的 `ctest -R surface|mcp|protocol|parity` 门禁无法全绿。处置：每个目标一行链接追加（在途 #1335 "restore master build/CI" 的同类修复面），冲突面仍为行级追加。
- `surface_registry.cpp/h`、`meta_protocol_tools.cpp`：**零改动**（只读引用）。
- `pi/exp-rs-spatial.ts`：**零改动**（生产逻辑不动；pi 侧断言全部落在 pi/test/）。
若后续 #1334 合并且冲突面实测过大 → 触发预案 rebase（Phase 7 预算）。

## D-2：快照 = 进程内 hermetic projection，而非真实二进制 wire dump

理由：(a) 比对器必须 hermetic 且确定性——真实二进制的 catalog 依赖 QGIS/GUI provider，机器间不稳定；(b) 真实二进制 leg 已由既有 `CLI tools list matches the projection` 用例以 superset 语义覆盖；(c) 进程内 `collectSurfaceTools()` 与 tools/list 是同一函数（mcp_server.cpp:601），快照锁的是"跨提交漂移"与"pi 消费逻辑"。快照带 `schemaVersion` 字段，加载自校验；再生成命令写入 CMake 注释与测试失败消息（防手改快照作弊）。

## D-3：缺 method 请求 -32601 → -32600 是缺陷修复而非行为破坏（review 后修订落点）

JSON-RPC 2.0：method 成员缺失 = Invalid Request（-32600）。现状 mcp_server.cpp 落入未知 method 分支回 -32601。修复影响面：仅"没有 method 成员"的帧；带未知 method 的帧仍 -32601。无客户端合法路径受影响（pi bridge 总是发 method）。按 TDD 先红后绿。
**review P2 #3 修订**：检查落点从 -32002 门之后移到之前——Invalid Request 约束的是"帧"，门约束的是"请求"，无 method 帧不是门可排序的请求；状态无关的帧分类（未初始化服务器上也回 -32600）语义更纯，新增用例钉死。

## D-4：WP-A/B 转"固化防回归"的依据（prompt 预案条款）

Phase 0 快照结论（BASELINE.md §5）：进程内三处 diff 已由既有测试约束近零，但 pi 消费端零逐工具锁死、无共享快照。按 prompt第四节"若 Phase 0 快照显示某侧已零漂移，WP-A/B 对应半边转为固化防回归，火力转移到 WP-C/D"，账本记录重分配。这不降低 3.2 门禁：比对器 + 15 用例照常交付。

## D-5：构建环境实测

cmake 需显式 PATH（/tmp/cmake-3.30.5-linux-x86_64/bin）；ninja 用 /home/kevin/pwb-sdks/root/usr/bin/ninja；`CMAKE_PREFIX_PATH=/home/kevin/pwb-sdks/root/usr`、`GSL_ROOT_DIR=/usr`、Qt6 系统级。系统 RAM 64 GB（可用 ~29 GB），-j2 恒定无需降级；机器 load 高（其他轨道并行编译），测试串行 -j1 红线维持。

## D-6：ADR 增补裁定

表面语义澄清（快照锁死程序 + pi 消费合同）按 ADR 0122 程序记录：不改 0122 决策本体，在其 Consequences 后追加实现注记，或新增轻量 ADR——待 WP-E 收口时依实际改动范围裁定（若仅测试机制则记 ADR-0122 注记即可，避免 ADR 膨胀）。

## D-7：账本落点 = `.planning/mcp-surface-r4/LEDGER.md`（非根目录 `.goal-loop-ledger.md`）

实测根目录 `.goal-loop-ledger.md` 为 git tracked（多轨道共享的已提交历史，来自 #1335/#1337/#1338/#1340 等先例）。向 tracked 共享文件追加 Track 9 内容 = 污染他轨道历史 + 超出白名单。按 prompt"gitignored"意图落 `.planning/*`（实测 gitignored；planning 工件 PR 时按 #1340 先例 `git add -f`）。

## D-8：piToolName / truncateTail 单源化搬迁（exp-rs-spatial.ts → mcp_bridge.ts）

pi 快照测试需要用**真函数**（非 regex 影子）验证映射与截断合同；两函数是纯函数，mcp_bridge.ts 是 node --test 可直接 import 的唯一 pi 模块（exp-rs-spatial.ts 依赖 Pi 扩展宿主不可 import）。搬迁 = 桥内新增导出 + 壳内删本地定义改 import。#1334 对 exp-rs-spatial.ts 的改动实测仅在头部文档注释（SICNU_MCP_WORKSPACE 段），与本改动区域不相交 → 自动合并无忧。

## D-9：修复 master 基线红的 5 个陈旧 pi 结构测试

实测 `node --test pi/test/` = 14 过 / 5 红。5 红全是测试侧陈旧：bridge_parity.test.mjs 的 3 个"both bridges"测试把生命周期 pin 迭代到 exp-rs-spatial.ts（该文件已按 no_drift 的单实现裁定剥离传输——两个测试文件互相矛盾）；no_drift.test.mjs 的溢出分支正则期望重构前形状、行为测试在 exit 事件前断言 alive。修复方向：pin 当前单实现形状（mcp_bridge.ts 持有 failDesyncedStream/finally/exit-reject；壳仅 import），行为测试改为轮询 exit（对齐 mcp_bridge.test.mjs 的既有轮询先例）。不改任何生产代码。
