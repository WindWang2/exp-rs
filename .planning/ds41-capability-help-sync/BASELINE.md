# BASELINE — ds41-capability-help-sync (Track D1: Capability Metadata, Help & Surface Sync)

Phase 0 只读预读完成于 2026-09-20（本地），全部基于本地 refs 事实（执行环境无外网，`git fetch` 失败；按 Track 约束以本地证据为准，不等待线上 CI）。

## 仓库状态

- `origin/master` = `adf8f98952422fe9c386c56d64d5fb6a4a6642f1`（"docs(agents): record Platform 5.0 audit request"）。
  Prompt 快照 `2761a6857` 已过时（快照后合入 #1115 等 1 个提交）。
- 本 track worktree/branch：`C:\Users\wangj.KEVIN\projects\exp-rs-worktrees\ds41-capability-help-sync`，分支 `agent/ds41-capability-help-sync`，基于 `adf8f9895` 创建。
- Open PR / open issue：执行环境离线无法 `gh` 查询；按 prompt 快照为 0/0，且本地 refs 无任何 track 分支覆盖本 Track 范围（见 DEDUP.md）。
- 远端历史分支 13 个全部 `behind=80 ahead≤12`，均判定为已合入/被 supersede 的历史残影，只作证据读取，不作基线。

## 本地构建/测试环境（实测）

- 工具链：MSVC 14.38.33130（VS 2022 Community）+ Ninja + CMake（`C:\Qt\Tools\CMake_64`）+ Qt 6.8.0 + vcpkg（`C:\deps\vcpkg`，manifest 模式）。
- **沙箱内 `cmd` 直连失灵**（banner 后即退）；PowerShell 调用 cmd 正常。所有 Windows 原生命令（vcvars/cmake/ninja/测试）经 PowerShell 启动。
- 测试运行需 PATH：`build-dev`、Qt bin、qca/kc bin、vcpkg_installed debug+bin，`QT_QPA_PLATFORM=offscreen`。
- 本 Track worktree 构建目录：`build-cap/`（复用 `build-dev/vcpkg_installed`，同 manifest 不重装依赖）。

## 关键发现：master 上两个 capability drift gate 均已 RED（可复现）

在 master 预编译二进制（build-dev，2026-09-18）上实测：

### 1. `tests/test_algorithm_meta_drift.cpp` — FAIL

```
REQUIRE( expectedCatalog.size() == 43 )
with expansion: 53 == 43
```

- live registry 有 **53** 个声明 taskFamily 的算法，而测试 pin 43、磁盘 Layer-A sidecar 51 个（缺 2 个文件，缺 10 个相对 live 集合）。
- 根因：Platform 10.0（rs:classify/change/regress）、Spectral Intelligence 11.0（local_rx_anomaly/sparse_unmixing/spectral_similarity/endmember_analysis）、SAR/InSAR 等新增算子后未重新 `--export-catalog`，pin 值也未更新。

### 2. `tests/test_capability_knowledge.cpp` — 12 cases / 8 FAILED，9 assertions FAILED

根因是**已提交进 master 的损坏数据**：

- merge commit `43dcf19cd`（"land PR #1022 terrain-hydrology-11"）对 `data/processing/algorithm_meta/capability/` 做了坏合并：`6a91a9b24` 时 `rs-mnf.json` 等 19 个文件 VALID，合并后变成**键重复的非法 JSON**（两份重新生成的内容被拼接，`"capability"` 键出现 2 次，`io`/`determinism`/`limitations` 等成对出现）。
- 19 个损坏文件（全部由 node `JSON.parse` 实测确认）：`rs-ace, rs-endmember-extraction, rs-gaofen-import, rs-hj-import, rs-library-select, rs-matched-filter, rs-mnf-inverse, rs-mnf, rs-sam-classify, rs-spectral-band-select, rs-spectral-unmixing, rs-temporal-extract-regions, rs-temporal-harmonic-breaks, rs-temporal-monitor, rs-temporal-phenology, rs-temporal-region-features, rs-temporal-regularize, rs-temporal-smooth, rs-terrain-flow`。
- `CapabilityCatalog::reload()` 记录 loadProblems 并跳过这 19 个 entry；另有 ≥6 个已注册算子**完全没有 sidecar**（`rs:sparse_unmixing, rs:spectral_similarity, rs:sar_remove_topographic_phase, rs:sar_coregister_local, rs:sar_pair_network, rs:sar_network_inversion`）。coverage REQUIRE（catalog.size()==registry.size()、loadProblems 空）必然红。

### 3. 完整性缺口量化（119 个可解析 sidecar）

| 指标 | 数量 | 说明 |
|---|---|---|
| 空 `summary` | 3/119 | rs-change, rs-classify, rs-regress（Platform-10 model-task adapters） |
| 空 `failure_modes` | 4/119 | rs-atmospheric-dos2, rs-change, rs-classify, rs-regress |
| 空 `io.inputs` | 23/119 | 传感器 import + temporal 集合类算子（输入为路径数组参数，非 raster 数据端口） |
| 完全无 units 声明 | 113/119 | `rs_schema.h` 的 make*Param 不接受 unit 参数；units 不是任何层的一等字段 |
| NoData 仅散落散文 | 21/119 提及 | 无结构化 NoData 契约 |

### 4. Surface parity 缺口（本 Track 核心）

- 已有 `tests/test_surface_parity.cpp`（cli-mcp-agent-surface-11, PR #1020 已合并）只 gate **tool 面**（projection↔MCP tools/list↔CLI tools↔Pi）。
- **没有任何测试比较 algorithm/capability 面**：CLI `--list`/`algorithms list`、MCP `list_algorithms`/`list_operators`、sidecar A、sidecar B、help `operator.*` 主题各自投影，互不一致时无 gate（如 CLI 与 MCP 的 `source` 词汇表 `plugin|builtin` vs `rs|provider` 不兼容；sidecar B 的 138 条富能力事实对 MCP/CLI 发现面完全不可见）。
- MCP `get_tool_help` 对无法解析 help id 的工具直接 throw，而 `get_algorithm_schema` 可成功——两者成功集无联动测试（G7）。

## 已有资产（不要重造）

| 资产 | 位置 | 状态 |
|---|---|---|
| Layer-A 生成器 | `AlgorithmMetaStore::exportCatalog` + CLI `--export-catalog` | 已存在，未跑 |
| Layer-B 生成器 | `scripts/capability_knowledge_tool.cpp`（gen-meta/gen-pages/dump，authored 键保留） | 已存在，未跑 |
| Layer-B 守门 | `tests/test_capability_knowledge.cpp`（coverage/drift/determinism/graph/pages/budgets/compose） | 已存在，当前 RED |
| Layer-A 守门 | `tests/test_algorithm_meta_drift.cpp`（membership/byte-for-byte/drift/roundtrip） | 已存在，当前 RED |
| help 主题自动派生 | `src/help/operator_help_provider.cpp`（每算子+每参数各一主题，curated 可缺） | 已存在 |
| tool 面 parity | `tests/test_surface_parity.cpp` | 已存在（只覆盖 tool） |
| 可行性知识镜像 | `data/agent/capabilities/*.json` + `test_capability_drift.cpp` | 已存在 |
