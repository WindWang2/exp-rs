# BASELINE — Track `ds41-fuzz-boundaries`（2026-09-20）

Track 目标：对外部 JSON、manifest、workflow、plugin IPC、路径、checkpoint、metadata store
建立系统性 fuzz/property corpus，以测试发现未来边界缺陷。

所有命令在主仓库只读执行；worktree 创建于预读完成后。

## origin/master 刷新（实时）

```
git fetch origin --prune
git rev-parse origin/master
→ adf8f98952442fe9c386c56d64d5fb6a4a6642f1
```

```
git log --oneline --decorate -8 origin/master
adf8f9895 (HEAD -> master, origin/master, origin/HEAD) docs(agents): record Platform 5.0 audit request
fe7da0622 feat(agent): add StepFun preset provider profile
2761a6857 Merge pull request #1115 from WindWang2/fix/issues-1097-wave2
28d3ffcb9 fix: deep-review wave-2 investigation targets (#1097)
08911c838 Merge pull request #1113 from WindWang2/fix/issues-ops-wf-agent-1076-1095
8a52c5b47 Merge pull request #1112 from WindWang2/fix/issues-app-1083-1093
4faa6a4a5 Merge pull request #1111 from WindWang2/fix/issues-pkg-proc-1087-1097
9a9d2e811 Merge pull request #1110 from WindWang2/fix/issues-geospatial-1081-1092
```

**Prompt 快照已失效**：Prompt 制作时快照为 `2761a6857`、open PR=0 / open issue=0。
启动时 master 已前进两个提交（`fe7da0622` StepFun preset、`adf8f9895` docs）。
实时状态：**open PR = 0、open issue = 0**（与快照一致，但这是在 master 前进之后重新确认的）。

## 已合入、与本 Track 范围直接相关的近期 PR

| PR | 标题 | 与本 Track 的关系 |
|---|---|---|
| #1103 | fix(plugin): harden worker trust boundary, IPC lifetime and typed JSON reads | **高度相关但已合入**。修了 SDK IPC/host 的 typed JSON 读、UI schema host 侧重验证、ui.invoke 嵌套。分支 `fix/r2-plugin-sdk-trust` 已删除。 |
| #1100 | fix(geospatial): make HTTP, mirror, metadata and JSON readers fail closed | 覆盖 geospatial JSON 读者 fail-closed；#1034/#1092 的 HTTP 语义在此修复。 |
| #1104 | fix(processing): check every write result, close error paths, bound operator inputs | 与本 Track 的 manifest/参数边界相邻，已合入。 |
| #1105 | fix(data): enforce atomic SQLite transactions and truthful store lookups | 覆盖 metadata store 事务边界（不是 parser，但 store 持久化入口相关）。 |
| #1106 | fix(agent): enforce MCP workspace containment on data tools | 路径 containment 的 agent 侧；`exprs::PathPolicy` 仍是唯一策略 owner。 |
| #1107 | fix(workflow): confine IR2 artifacts to the run directory | workflow 产物路径约束。 |
| #1115/#1113/#1112/#1111/#1110 | 2026-09-19 大规模 fail-closed 波次 | 均已合入，本 Track 不复现其范围。 |

## 远端历史分支判定（`git log origin/master..origin/<branch>` + `git diff --stat`）

| 分支 | 未合入提交 | 判定 |
|---|---|---|
| `agent/ds41-http-fetch-strict` | 2 | **superseded**：其 #1034 修复与 master 现有实现不等价且更弱（master 保留了 `throwHttpErrors` 的 404/410 分支并修了 truncation 语义，#1100/#1092）。不 cherry-pick。 |
| `agent/glm53-plugin-sdk-trust` | 7 | **superseded by #1103**：其 fix 范围（#1036/#1039/#1040/#1041/#1038 的 SDK 侧）由 #1103 以不同分支合入；`src/sdk/exprs/json_reader.h` 等文件不存在于 master。#1103 的测试文件（`test_exprs_ipc.cpp` 等）已在 master。 |
| `agent/flash-workflow-integrity` | 8 | **superseded**：工作流 checkpoint/resume 修复已由 #1105/#1106/#1107/#1113 系列合入；`tests/test_workflow_checkpoint_*` 在 master 已存在。 |
| `agent/flash-data-transaction-integrity` | 4 | **superseded by #1105**。 |
| `agent/flash-geo-fabric-integrity` | 4 | **superseded by #1100/#1110**。 |
| `agent/flash-processing-atomic-errors` | 5 | **superseded by #1104**。 |
| `agent/flash-mcp-containment-routing` | 5 | **superseded by #1106**。 |
| `agent/flash-lab-foundry-determinism` | 6 | **superseded by #1107**。 |
| `agent/glm53-desktop-lifecycle` | 12 | **superseded by #1101/#1102/#1112**。 |
| `agent/ds41-pipeline-drag-lifetime` | 4 | **superseded by #1112**（#1085）。 |
| `fix/ci-master-unblock`、`fix/r2-ci-protobuf-multimode`、`fix/review-issues-1033-1056` | 1-2 | CI 修复残留，与 master CI 状态无关；本 Track 不等 CI。 |

→ **无任何未合入增量需要继承**；全部按历史证据读取。

## 源码级 census（与本 Track 入口相关的解析/持久化边界）

### 已有 fuzz/property 覆盖（Verification 7.0/8.0 "task D" 系列）

| 入口 | 覆盖文件 |
|---|---|
| `ResourceUri::parse/display/canonical/resolveAgainst` | `tests/test_contract_fuzz_io.cpp` |
| `worker_protocol parseFrame`、`PluginManifest::fromJson`、`SplitConfig::fromJson/validate` | `tests/test_contract_fuzz_ipc.cpp` |
| `DatasetManifest`（未知字段容忍、typed reject） | `tests/test_contract_fuzz_data.cpp` |
| MapSpec 条件 AST + workflow placeholder 语法 | `tests/test_contract_fuzz_lang.cpp` |
| AgentPlan reader + MapSpec validate/patch | `tests/test_contract_fuzz_agent.cpp` |
| operator schema（ModelCatalog）参数投影 | `tests/test_contract_fuzz_ops.cpp` |
| （已知答案腿）IPC frame/envelope、UI schema | `tests/test_exprs_ipc.cpp`、`tests/test_plugin_ui_schema.cpp`、`tests/test_plugin_ui_schema_host.cpp` |

共享生成器：`tests/support/bounded_fuzz.h`（固定 seed xorshift64* + mutate）。

### 未覆盖缺口（本 Track 主目标）

| 入口 | 源码 | 现有测试 | 边界风险 |
|---|---|---|---|
| `IpcFrame::writeJson/read`（长度前缀 framing） | `src/sdk/exprs/ipc_frame.cpp` | 已知答案腿（`test_exprs_ipc.cpp`） | 截断流、暂停续传、超上限宣告（E6003）、u32 前缀溢出 |
| `Ipc::decodeEnvelope` / `IpcError::fromJson` | `src/sdk/exprs/ipc_envelope.cpp` | 已知答案腿 | 错类型、版本突变、非法 id、缺 error code |
| `exprs::PathPolicy`（plugin/workspace containment） | `src/sdk/exprs/path_policy.cpp` | 仅 `test_exprs_external_process_win.cpp` 触及 | 遍历、Unicode、绝对/盘符、canonical 包装 |
| `exprs::PluginPackage::versionSatisfiesRange` | `src/sdk/exprs/plugin_package.cpp` | 无 | semver range 解析边界 |
| `exprs::PluginPackage::install`（zip-slip） | 同上 | 无 fuzz | package 阶段路径逃逸 |
| `exprs::validatePluginUiSchema` / `validateUiEvent` | `src/sdk/exprs/plugin_ui_schema.cpp` | 已知答案腿 | 错类型字段（#1038 同类）、cap 强制、组深度 |
| `exprs::validateWorkflowDocument` / `migrateWorkflowDocument` | `src/sdk/exprs/workflow_schema.cpp` | `test_exprs_workflow_schema.cpp`（已知答案） | schema_version 突变、step 错类型 |
| `PluginDiagnostic::fromJson` | `src/sdk/exprs/plugin_diagnostics.cpp` | 无 | 错类型 code/severity |
| `PluginIndex::applyPins` | `src/sdk/exprs/plugin_index.cpp` | 无 | 手改索引 |
| `workflow_checkpoint`（checkpoint 截断/恢复） | `src/workflow/workflow_checkpoint.cpp` | `test_workflow_checkpoint_cache.cpp`（Qt 重闭包） | 截断、半写、版本不匹配 |
| `WorkflowDefinition`（JSON → IR） | `src/workflow/workflow_definition.cpp` | 多个 e2e 使用，无 fuzz | 错类型节点/边、DAG 突变 |
| metadata store（SQLite 3 store） | `src/data/*` | `#1105` 语义测试 | 事务边界（已由 #1105 覆盖语义；本 Track 以持久化载荷边界为补充） |

### 关键 seam 事实（决定实现方式）

- `sicnu_sdk` 是 **STATIC、仅依赖 jsoncpp、无 Qt** 的库，包含 exprs 全部解析器
  （`src/sdk/CMakeLists.txt`）→ fuzz target 可用 `sicnu_add_sdk_test`（Catch2 + sicnu_sdk）。
- `tests/support/bounded_fuzz.h` 是既有共享生成器（固定 seed、硬上界、total function）。
- jsoncpp 1.9.6：`CharReaderBuilder` 有 `stackLimit`（默认低），深度超限抛 `Json::Exception`
  → parse 层不会栈溢出；**危险在于业务层对错类型值的 `as*()` 转换**（#1038 的类别）。
- CMake 约定：fuzz target 追加在 `tests/CMakeLists.txt` 末尾（7.0/8.0 "task D" 块），
  共享文件 append-only。
- 测试运行要求 `QT_QPA_PLATFORM=offscreen`；`ctest -j1`；编译 `-j1`（必要时 `-j2`）。
- `.planning/<track>/*.md` 按 `.gitignore` 白名单约定提交（markdown only）。
