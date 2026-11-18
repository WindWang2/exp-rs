# R33-T05 PATH_OWNERSHIP — 运行时排他表

刷新时间：2026-11-18（建树时）。提交 PR 前必须再次刷新。

## 开放 PR 保留区

**无** —— #2134/#2135/#2136/#2137 已全部合并（2026-11-17T14:16–14:17Z），#2139 后续合入。
快照所列 exclusive_paths 已进入 master；本轨通过 master 继承，不重写其修复、不cherry-pick。

## 本轨写集（首批）

| 路径 | 工作包 | 依据 |
|---|---|---|
| `src/app/workbench/mission_timeline_model.cpp/.h` | WP02 | 分页 append 通知契约 |
| `src/agent_loop/session_journal.cpp/.h` | WP05/WP06 | seq 边界 + compaction 终态语义 |
| `src/app/workbench/mission_context_store.cpp` | WP08 | authority sidecar 有界读 |
| `src/app/workbench/mission_timeline_store.cpp` | WP08 | legacy timeline 有界读 |
| `src/app/workbench/mission_runtime_store.cpp` | WP08/WP09 | last-good 有界读 + 同字节证据 |
| `src/agent_ops/secret_redactor.cpp/.h` | WP11 | fault 子树脱敏 |
| `src/agent_ops/delivery_assembler.cpp/.h` | WP13 | 未知 schema report fail-closed + 外部 claims 标记 |
| `tests/test_r33_t05_*.cpp`（新增） | 各 WP | 回归测试 |
| `tests/CMakeLists.txt` | — | 仅追加本轨唯一 target 块 |

## 明确不写

- `src/agent_ops/operations_coordinator.cpp`（#2078 修复不在本轨；即使已合并，保持最小改动边界，由读者复核现状）
- `src/agent_loop/decision_record.cpp`（#2079；#2134 已改过该文件，本轨不重复触碰）
- `resources/translations/sicnu_zh_CN.ts`（T06 统合；本轨无新增用户可见文案时无 HANDOFF 需求）
- `src/agent/**`（dock/runtime/PiProcess/AgentToolHost——#2134 领域）
- TaskCenter/OutputCommitter/workflow/公共 verify（T04）；data/geo/portable/atomic_fs/durable_sidecar（T03，本轨只调用）
- 模型 runtime / plugin bridge（T02）；普通桌面 UI/制图/Studio（T06）

## 跨轨依赖（只读消费）

- `src/platform/durable_sidecar.h`（T03）：sidecar 写权威 + `kDefaultMaxSidecarBytes`(32MiB) —— 本轨读取预算与之对齐
- `src/agentbench/*`：fault marker 合同（string kind，evaluator 只认 isString）
