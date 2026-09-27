# PLAN — Track 9 MCP/Pi Surface Parity R4

## 目标（Oracle 摘要）
1. 三处表面（C++ SpatialTool registry / MCP tools/list / pi 消费面）diff 清零，由可复跑比对器（快照 + 双端用例）锁死；比对器经"故意改一端变红"自证。
2. 协议健壮性用例 ≥ 15（C++ ≥ 9 + pi ≥ 6），双侧输出逐一可数。
3. `ctest -R "surface|mcp|protocol|parity" -j1` 全新构建目录连续两轮全绿；`node --test pi/test/` 连续两轮全绿。
4. ≥ 10 原子提交、≥ 12 触碰文件、全部在白名单内、零新方向。
5. 独立 review（1 subagent 槽位）+ 证据链（EVIDENCE/REVIEW_LOG）+ PR。

## Phase 计划（预算见 prompt 3.3）
- Phase 0 ✅：worktree + BASELINE.md + 漂移快照结论（本文件 + BASELINE.md 第 5 节）。
- Phase 1（WP-A 固化防回归）：`tests/surface_diff_snapshot.json` 生成器（in-process hermetic projection）+ C++ 比对用例（registry ↔ projection ↔ tools/list ↔ snapshot 四方）；已知进程内 parity 近零 → 转"固化"，账本记录重分配。
- Phase 2（WP-B）：pi 侧快照消费用例（piToolName 映射、类目过滤、名称冲突、快照 schema 版本校验）进 `pi/test/surface_snapshot.test.mjs`；类目清单与 C++ surfaceAllowedPrefixes/surfaceFamilies 双向一致（经快照文件）。
- Phase 3（WP-C，≥9 C++ 用例，扩 `tests/test_surface_protocol.cpp` 追加式）：非 JSON 帧 -32700；行超长哨兵 -32700；非对象帧 -32600；缺 method 请求 → **修为 -32600**（现状 -32601 违反 JSON-RPC 2.0，最小修复 + 新用例）；未初始化 -32002 门；cancelled→TaskCenter 映射 + 竞态窗口（终态后取消无双发）；显式 null id 合同（#644）；tools/call arguments 非对象 -32602；spatial 工具缺必填参（error 含字段名）+ 类型错位；tools/list 前缀过滤合同（含 custom_tools: 排除）。
- Phase 4（WP-D ≥6 pi 用例，扩 `pi/test/`）：AbortSignal 竞态（abort 后 request 不 resolve 且 cancelled 恰一次）；cancelled 通知后状态复位；非法帧恢复；缺参调用结构化错误；truncated 标记透传；schema 往返等价（经快照）。WP-E：快照比对器红测自证 + 双侧全量双跑。
- Phase 5：独立对抗性 review（subagent #1）；P0/P1 修复。
- Phase 6：EVIDENCE.md / REVIEW_LOG.md / 提交整理 / PR。
- Phase 7：预留（补深 / #1334 rebase 监测 / 双跑复核）。

## 提交切片（≥10 原子提交草案）
1. planning: Phase 0 工件（BASELINE/PLAN/DECISIONS + ledger）
2. WP-A: snapshot generator + C++ comparator 用例 + snapshot 文件
3. WP-B: pi/test/surface_snapshot.test.mjs（映射 + 类目 + 冲突）
4. WP-C batch 1: 帧级恶意输入三用例（-32700/-32600/-32700 哨兵）
5. WP-C fix: 缺 method → -32600（实现最小修复 + 用例）
6. WP-C batch 2: -32002 门 + null id 合同 + arguments 非对象
7. WP-C batch 3: cancelled→TaskCenter 映射与竞态 + 无双发
8. WP-C batch 4: spatial 工具参数健壮性（缺参字段名 + 类型错位）+ tools/list 过滤合同
9. WP-D batch 1: pi 取消语义两用例 + 非法帧
10. WP-D batch 2: truncated 透传 + schema 往返 + 缺参
11. WP-E: 比对器红测自证记录 + 快照再生成命令文档化
12. planning: EVIDENCE/REVIEW_LOG/PR_BODY

## 验证矩阵
- `ninja -C build-r4 -j2 <targets>`（编译门）
- `ctest --test-dir build-r4 -R "surface|mcp|protocol|parity" -j1`（双跑）
- `node --test pi/test/`（双跑）
- 快照红测自证：临时改快照 → ctest 红 → 还原 → 绿（账本记录）
