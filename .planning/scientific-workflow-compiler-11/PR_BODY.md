# PR: feat(harness): scientific workflow compiler & grounding 11.0 — fact model 2.0, bounded probes, execution provenance

> Local evidence only; no online CI dependency.

## Baseline & parallel-track dedupe

- Baseline: `origin/master@a5b11b7f`（启动时刷新；prompt 快照 ebcafb4d 已过期，#991/#992 已合入）。
- #1009 (execution-runtime-convergence-11): 主战场 src/runtime/** 与 `src/workflow/pipeline_run_coordinator.cpp` —— 本 PR 零交集（全部落在 harness/）；共享 tests/CMakeLists.txt、CHANGELOG、.gitignore 均为 append-only。
- #1008 (radiometric-spectral-workbench): 主战场 spectral/radiometric —— 本 PR 只读消费 master 已有 radiometric 语义；不触其文件。
- 7 个 open issues（#1001–#1007）逐条 dedupe：全部不在本 track ownership（见 BASELINE.md issues 表）；未实施。

## What lands

1. **fact model 2.0** (`workflow_facts.*`): 时间 cadence/regularity/coverage（QDateTime 锚定 + 手算 oracle）、unit-aware 分辨率（WKT UNIT 解析 + 闭表类别，degree 诚实 unknown_meters）、extent 校验、quality mask 词表（与 producer round-trip 互钉）、product generation 机械解析、model task 闭表规范化、resource facts 双侧已知才判定。全部 per-key fact_status。
2. **bounded grounding probes** (`grounding_probes.*` + 工具 `harness:probe_facts`/`harness:probe_model`): 闭表 scope、I/O 前校验、唯一 resolver、复用唯一 understanding 缓存（`understandingCacheKeyFor` 公共化）、诚实超时记账、stat-only model manifest 探针。
3. **analysis 2.0**: temporal_calendar / numeric_domain_chain / band_identity / output_identity 四检查族 + 四个 additive 错误码；UNKNOWN 一律 skip 不伪 PASS。
4. **repair 2.0**: `planPreparedDecisions` —— risk/cost/evidence 确定性排序的 prepared decision 文档；`IrRepairRecord.params` 使已应用 repair 可复放；拒绝项永不 auto-applicable。
5. **execution provenance projection** (`provenance_projection.*`): `metadata.compiler` 规范块（digest/truncated_keys 诚实）+ `<output>.compile.json` QSaveFile atomic sidecar；planner lower 阶段接线；`src/workflow/**` 零改动。
6. **explain** (`workflow_explain.*`): compile→run→failure 因果回溯，zh-CN 闭表文案、8 cause 上限、序列化字节预算计量。
7. **eval corpus**: temporal-calendar 冲突 / numeric-domain-chain / probe surfaces 三类 case（数据追加，runner 零改动）。
8. **Pi/ZCode bridge**: `pi/test/scientific_workflow_compiler_11.test.mjs` —— 编译器表面钉在共享 McpBridge、跨语言 schema 锚、knowledge 页预算、满额 IR round-trip（宿主不支持 shebang spawn 时 canary 诚实 skip）。
9. **planner**: collection descriptor grounding（时间集 facts）与 projection 附加；ADR 0163。

## Architecture decisions

见 `.planning/scientific-workflow-compiler-11/DECISIONS.md`（D-001…D-014）与 `docs/adr/0163-scientific-workflow-compiler-grounding-11.md`。

## Compatibility

- 全部 additive：新错误码只进唯一 taxonomy 表；`kFactKeys` 仅追加 `dates`；`kOverrides` 84→86（追加 2 条）。
- IR/analysis/repair 既有契约不变（既有测试全部照旧）。
- `metadata.compiler` 为引擎忽略的未知根键（ADR 0149 已验证）。

## Local tests（如实状态）

已执行：
- 12 个新/改 C++ 源文件 + 4 个测试文件：`ninja -t commands` 提取实际编译命令逐个编译，12/12 通过。
- `node --test pi/test/scientific_workflow_compiler_11.test.mjs` → 5 tests / 3 pass / 0 fail / 2 skip（skip 原因：本 Windows 宿主无法 exec 带 shebang 的 fake MCP server —— master 上现存的 `pi/test/no_drift.test.mjs` 行为段同样失败，pre-existing 宿主限制，canary 显式 skip 并打印原因）。
- `git diff --check origin/master...HEAD` 干净；冲突标记/secret 扫描干净。
- 发现并修复 pre-existing master 缺陷：`src/workflow/pipeline_run_coordinator.cpp` Q_OS_WIN 分支缺 `<fcntl.h>`（`_O_WRONLY/_O_BINARY` 未声明，Windows 无法编译 sicnu_workflow）→ 独立 commit，1 行 additive；修复后该目标文件在后续构建中编译通过（仅安全警告 C4996）。

**未执行（not-executed）**：4 个 `test_*_11` 目标的完整链接 + `ctest -R '*_11'`、`ctest -R test_harness_eval_corpus`、既有 harness 回归套件。原因：宿主内存持续 92–94%（多轨道并发构建挤占），全量 -j1 构建需数小时；**按用户明确指示停止构建循环、先行提交 PR**。合并前需在可调度资源上补跑上述 gate；本轮不做"已验证"声明。

## Resource evidence

## Resource evidence

- 构建 dev-default（Ninja）+ VS2022；硬上限 -j2 → 内存 94% 时按 GOAL 降档 -j1；测试 -j1 + QT_QPA_PLATFORM=offscreen。
- Git Bash 无 load average（not-executed 记录于 EVIDENCE）。

## Known limitations / follow-ups

- 探针超时为诚实记账而非硬中断（同步工具无法中断）——异步可中断 grounding 为后续 track。
- 引擎消费 `metadata.compiler` 属执行平面 follow-up（#1009 系）。
- pi 行为测试在本 Windows 宿主 skip（master 的 no_drift 同样受限）；Linux CI 正常执行。

## Review

（Phase 7 后填充 REVIEW_LOG 摘要。）
