# DEDUP — Track `ds41-fuzz-boundaries`（2026-09-20 实时）

## GitHub 状态（启动时）

- open PR = **0**；open issue = **0**（`gh pr list --state open`、`gh issue list --state open`）。
- 与 Track 范围相关的近期 merged PR：见 `BASELINE.md` 表格（#1100–#1107、#1110–#1115）。
- 2013–2107 区间无任何 Track 同名/近名 PR → **无 PR 级重叠**。

## 功能级重叠判定

### 1. `test_contract_fuzz_*` 既有系列（Verification 7.0/8.0 task D）

**判定：不重复。** 既有系列覆盖 ResourceUri / worker_protocol / PluginManifest /
SplitConfig / DatasetManifest / MapSpec / AgentPlan / operator schema。
本 Track 覆盖 **framing（ipc_frame）、envelope、path policy、plugin package version range、
UI schema、workflow schema、diagnostics** —— 全部是"已知答案腿已有、mutation corpus 没有"的入口。
本 Track 的做法是**补 mutation/property 腿**，并在 CMake 注释中显式引用既有腿的文件名，
不复制既有断言。

### 2. `agent/glm53-plugin-sdk-trust` 分支（7 未合入提交）

`gh pr view 1103` + 文件比对：#1103 从 `fix/r2-plugin-sdk-trust` 合入了 SDK IPC/host 的
typed-JSON 硬化（`src/plugins/host/plugin_host_process_runtime.cpp`、`plugin_host_proxies.cpp`、
`src/sdk/exprs/ipc_stream.*`、`plugin_manifest.cpp`、`plugin_package.cpp`、`workflow_schema.cpp`、
`tests/test_exprs_ipc.cpp` 等）。该分支的 `tests/fixtures/hostile_ui_worker/`、`src/sdk/exprs/json_reader.h`
在 master 不存在，但其**修复语义已被 #1103 覆盖**（issues #1036/#1039/#1040 由 #1103 关闭）。

**判定：superseded，不 cherry-pick、不复制。** 本 Track 的 fuzz lane 验证的是 #1103 之后
仍残留的未硬化面（见下）。

### 3. 源级审计发现的两个**未被任何 PR 覆盖**的残留面（本 Track 用 fuzz 证明后处理）

(a) `src/sdk/exprs/plugin_ui_schema.cpp` — `validateEntries()` 内 `commandId.asString()`
在 `commandId` 非字符串时进入 fail 分支并抛出 `Json::LogicError`。调用方三处
（`plugin_host_worker_main.cpp:1181`、`plugin_host_process_runtime.cpp:488`、
`plugin_ui_schema_host.cpp:457`）都有 try/catch → 当前无进程级崩溃，但违反该文件
自述契约（"must fail VALIDATION, never throw through the worker"）。

(b) ~~同文件 `validateUiEvent()` 的 `contributionId`/`controlId` 非字符串时抛出~~
— **核查后不成立**：master 上 `boundedString()` 已先于 `asString()`（既有守卫），
`validateUiEvent` 在其调用方无 try/catch 也不构成逃逸。reviewer F7 指正后更正；
对应测试腿改为纯 totality 断言（明确标注不是 defect regression）。

(c) `src/sdk/exprs/plugin_manifest.cpp:83` — `ManifestPort::fromJson` 中
`json.get("required", false).asBool()`：`required` 为字符串/数组时 `asBool()` 抛
`Json::LogicError`。#1103 硬化的是 SDK 的 `requireString/readInt` 帮手与 host 侧，
`ManifestPort` 这条路径未被覆盖（`test_contract_fuzz_ipc.cpp` 只喂整体 manifest JSON，
mutation 命中率不足以稳定触达该分支）。**需 fuzz 实证后决定是否修。**

### 4. workflow checkpoint / definition

master 已有 `test_workflow_checkpoint_cache.cpp`（Qt 重闭包，e2e 级）。
本 Track 以 **checkpoint 载荷截断/损坏** 的 property lane 为补充，只在其上追加
`tests/` 侧文件，不改其源码。闭包过重（sicnu_workflow + TaskCenter + Qt Widgets）
则降级为"记录为 known limitation + 留 issue"，不在本 PR 引入重依赖。

### 5. metadata store（SQLite）

事务/原子性语义已由 #1105 覆盖（`tests/test_governance_store.cpp` 等）。
本 Track 不重复其语义测试，仅在其载荷边界（JSON 列值类型）上做轻量 property，
且以既有 store 测试的 fixture 复用为前提。

## 结论

无 PR/issue 冲突。两个残留缺陷面（3a/3b/3c）属于本 Track owner 范围内可最小修复的
P1/P2；均在 fuzz 证明后按 OWNERSHIP 例外条款处理。
