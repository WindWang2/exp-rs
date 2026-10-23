# R20-03 OWNERSHIP — 文件级所有权声明

基线 `28e1891390`（origin/master）。分支 `agent/r20-03-pi-mcp-workflow-20261022T160809Z`。

## 允许写区（任务书 §3）
`pi/**`、`src/agent/**`（排除 cartography/mapspec/layout_tools/symbology 四红区）、`src/agent_loop/**`、`src/agent_ops/**`、`src/agentbench/**`、`src/workflow/**`、`src/sdk/**`、`src/plugins/**`、`src/preflight/**`、`src/science_context/**`、`src/scientific_state/**`、`src/planner/**`、`src/repair_planner/**`、`src/contracts/**`、`src/explain/**`、对应测试、`.planning/zcode-r20-03/**`。

## 实际改动集（git diff --name-only 28e1891390...HEAD，提交时逐项对照）

| 文件 | Owner | 合法性 |
|---|---|---|
| `src/sdk/exprs/ipc_envelope.cpp` | 本轨（src/sdk/**） | W1：IPC Response result 类型门（请求侧 params 已有门） |
| `src/sdk/exprs/ipc_frame.cpp` | 本轨（src/sdk/**） | W4：TooLarge 诊断抢救长度前缀 |
| `src/agent/harness/capability_catalog.cpp` | 本轨（src/agent/**，非红区） | W2：validateEntry/reload 类型门 |
| `src/agent/harness/capability_relations.cpp` | 本轨 | W2：validateRelations schema_version 类型门 |
| `src/agent/harness/capability_knowledge.cpp` | 本轨 | W2：loader + validateEntry id/family/kind/extends 类型门 |
| `src/agent/harness/grounding_tools.cpp` | 本轨 | W3：decision_record 参数类型门 |
| `src/agent_ops/operations_coordinator.h` | 本轨（src/agent_ops/**） | W5：删除死字段 approvePendingRepair（零读零写） |
| `tests/test_exprs_ipc.cpp` | 本轨（对应测试） | W1/W4 RED+回归 |
| `tests/test_capability_knowledge.cpp` | 本轨 | W2 RED+回归 |
| `tests/test_harness_evals.cpp` | 本轨 | W3 RED+回归 |
| `data/processing/algorithm_meta/capability/rs-sam-classify.json` | 本轨（R19-02 T6 先例：capability sidecar 生成物） | 跨轨修复：gen-meta 确定性再生（157 sidecar 单文件 diff），修复 r18-a #1663 漏再生导致的 drift 门 master 红 |
| `pi/knowledge/capability-classification.md` | 本轨（R19-02 T6 先例：gen-pages 生成物） | 上述 sidecar 的 gen-pages 传播（20728B<32KiB 预算门绿） |
| `.planning/zcode-r20-03/**` | 本轨 | 账本 |

## 红区遵守证明
- `src/agent/cartography/**`（CT-6 的 7 工具 schema 缺项）：**只读**，仅通过 test_surface_snapshot 的 registry 腿失败信息给出证据（04 轨职责）。
- `src/cli/**`：灰区。发现 D9（CLI `--approve` 布尔永不被消费、`--approve-pending-repair` 必被拒、无令牌通道）——r18-c 先例将其列为只读，本轨**只报告不修**（CROSS_TRACK_FINDINGS.md）。
- 其余禁写区（src/app、src/geospatial、src/data 等）零触碰。

## 共享文件
- `tests/CMakeLists.txt`：本轮三个测试文件均为**已注册**目标，无需新增注册段。若 review 阶段新增测试文件则按 append-only 唯一标记小段处理。
- 根 `CMakeLists.txt`/`CLAUDE.md`/`docs/adr/`：零触碰。
