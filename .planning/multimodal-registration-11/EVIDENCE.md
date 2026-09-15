# EVIDENCE — multimodal-registration-11

## Phase 0（2026-09-15）

- `git fetch origin --prune`：首次 TLS EOF（网络抖动），重试成功。origin/master=`a5b11b7f10fa010c1c060864fb427d777ba9a4aa`。
- `gh pr list`（GraphQL）连续 3 次 EOF → 改用 REST `gh api repos/.../pulls?state=open` 成功：唯一 open PR #1008（spectral）。
- `gh issue list` 成功：#1001–#1007（dedupe 见 BASELINE.md；#1005 in-scope）。
- `git branch -r --sort=-committerdate`：除 itk-upstream 外只有 `origin/zcode/radiometric-spectral-workbench`（= PR #1008）。
- PR #1008 changed files 通过 REST 拉取成功（53 个文件清单见 PARALLEL_OWNERSHIP.md；count 查询再次 EOF，清单本身完整）。
- ISSUES.md / CHANGELOG.md / docs/agents/goal-template.md 已读；ISSUES.md 全条目 dedupe 结论见 BASELINE.md（无本 track 工作）。
- Subagent #1（Explore，只读）完成 geometric 域深度审计：14 个主题 + A–H gap table（结论内嵌 BASELINE.md / CURRENT_ARCHITECTURE.md）。
- 资源基线：16 核 / 64 GiB / load 3.23（启动时）。build `-j2` 上限执行。
- worktree 创建：`git worktree add ../exp-rs-multimodal-registration-11 -b zcode/multimodal-registration-11 origin/master` → exit 0 @ a5b11b7f10。

## Build 基线（Phase 1）

- preset 实名 `dev-default`（GOAL 写的 build-dev 是 binaryDir）。`cmake --preset dev-default` exit 0。
- FetchContent 网络克隆 pybind11/catch2 反复 TLS EOF → 用 `-DFETCHCONTENT_SOURCE_DIR_PYBIND11/CATCH2` 指向主仓库 build-dev/_deps 缓存绕开网络。
- 全量 `all` 构建遇 master 预存编译缺陷（见 OUT_OF_SCOPE），后改为 targeted targets 构建：`cmake --build build-dev -j2 --target sicnu_processing sicnu_operators sicnu_agent qgis_app_georef + 13 个测试可执行`，exit 0。
- 资源：构建期间 load 13-17（阈值 24=1.5×16 核，维持 -j2），RSS 峰值 ~13.5/64 GiB。同主机有 3+ 个并行 track 在同时构建（load 主要来源，非本 track）。

## OUT_OF_SCOPE 补充（预存缺陷处置）

- **P0-blocker（已做最小修复，1 行）**：`src/app/workbench/mission_context_store.cpp` 缺 `#include <QDir>`（D18 引入；主仓库 build-dev 无该 .o，独立 g++ -std=c++20 语法检查复现）。不做修复则全树 `all` 无法构建，阻塞所有并行 track。
- **P0-blocker（已做最小修复，1 行）**：`src/agent/data_platform_tools.cpp` 使用未限定 `BenchmarkService`（D19 引入；主仓库 .o 日期早于 D19 commit，即本机从未编译成功）。补 `using sicnu::experiment::BenchmarkService;`，否则 sicnu_agent（本 track 依赖）无法编译。
- **不修（记录）**：`test_capability_drift` 在 master 上已红：preprocess.json 重复 id（rs:gaofen/zy3/hj_import ×2）、uncovered io:catalog_search/io:cache_prefetch/io:cube_plan/io:cube_window、rs:mnf_inverse、rs:library_select、rs:spectral_band_select、cartography:diff_templates/explain/export、recipe drift（harness.optical_ndvi_landsat）。本 track 已为新增表面（geometric.json）补齐 knowledge，使 uncovered 列表不因本 track 恶化。
- **不修（记录）**：`test_mission_context` / `test_mission_e2e_scaffolding` 链接失败（target 源清单缺 workbench_host.cpp）。

## 测试证据（targeted，QT_QPA_PLATFORM=offscreen，-j1 串行）

（待 Phase 8 双遍验证后填终值；中间轮次见 .goal-loop-ledger.md）

## OUT_OF_SCOPE

- #1001 io:clip CRS override（P1，io 域）
- #1002 workflow registry artifact 校验（P1，workflow 域）
- #1003 dataset join null 列（P1，dataset 域）
- #1004 dataset:qa scan_capped identity（P1，dataset/agent 域）
- #1006 PipelineRunCoordinator syntheticExecute 默认（P2，workflow 域）
- #1007 dataset:qa CRS 审计缺失（P2，dataset 域）
以上问题在 PR_BODY 顶部转述（P1 级），本 track 不修复。
