# PARALLEL_OWNERSHIP — scientific-contract-verification-11

审计时间：2026-09-16（baseline `a5b11b7f`）。规则：open PR / 本地并发分支的 changed files 一律 read-only；本 track 业务主体落在 primary write scope。

## 本 track write scope（审计后确认，未扩大业务面）

- `src/contracts/**`（新增 determinism census / ladder 数据模型）
- `tests/test_*contract*.cpp`、`tests/test_*verification*.cpp`（新增 11 系列测试）
- `scripts/verification_ladder.py`（匹配 `scripts/*verification*`，capability-aware 化）
- `data/contracts/**`（新增 snapshot/exemption 生成物）
- `docs/verification/**`
- **受控扩展（有理由的最小点改）**：`src/operators/**` 内 per-operator `determinismGrade()`/`determinism()` 显式覆盖与 `src/processing/algorithm_meta/capability` sidecar 修正（package B 的 mission 硬需求；这些文件无任何 open PR 触碰，逐条改动一行级，逐条可归因）。`tests/CMakeLists.txt` append-only 接线；`.gitignore` append-only 白名单。

## Open PR overlap

| PR | 分支 | 状态 | 与本 track 文件交集 | 策略 |
|---|---|---|---|---|
| #1008 radiometric-spectral | `zcode/radiometric-spectral-workbench` | open, CONFLICTING | **无文件交集**（spectral/radiometric 算法与 widgets） | 不碰其文件；其光谱算子若进 master 后由 contract registry 家族模板自然收编（master 已有 spectral_indices 契约族；新算子由后续 track 收编） |
| #1009 execution-runtime | `zcode/execution-runtime-convergence-11` | open, MERGEABLE | 潜在交集：`src/operators/framework/`（其新增 chunked_run/rs_operator_error/rs_operator_context；我**不改 framework**）；`tests/CMakeLists.txt`（双方 append-only，rebase 时按行合并）；`.gitignore`（双方 append-only） | contract 层不 import 其 runtime API；失败/取消语义契约引用 master 已有 ErrorCode/RSOperator seam，不依赖 #1009 新 API。rebase 冲突时按 append-only 语义人工合并 |

## 本地并发 11.0 worktrees（未推送）

| worktree | 推断主题 | 与本 track 交集判断 |
|---|---|---|
| advanced-insar-platform-11 | InSAR/PolSAR | 无（算法域） |
| cn-eo-product-physics-11 | 传感器物理 | 无（算法域） |
| execution-runtime-convergence-11 | = PR #1009 | 见上 |
| geoai-promptable-foundation-platform-11 | GeoAI | 无（模型域） |
| linked-visual-analytics-11 | 可视分析 | 无（UI 域） |
| scientific-workflow-compiler-11 | workflow 编译器 | 潜在 LabSpec surface；我只读消费 master 侧 seam |
| spectral-intelligence-11 | 光谱智能 | 与 #1008 类似域；无交集 |
| teaching-lab-platform-11 | 教学 lab | 无 |
| temporal-intelligence-11 | 时序智能 | 无 |

## master 上的权威 seam（本 track 消费而非复制）

- live operator registry：`src/operators/rs/rs_operators_init.cpp` `initBuiltinRsOperators()` 等（只读）
- determinism stamp：`rs_schema.cpp` `stampDeterminismGrade`；`RSOperator::determinismGrade()`/`determinism()`（只读 + 最小点改见上）
- capability sidecar loader：`src/agent/harness/capability_catalog.{h,cpp}`（只读；sidecar JSON 数据修正属数据文件）
- contract graph：`src/contracts/contract_graph.*`（本 track own）
- ladder：`scripts/verification_ladder.py`（本 track own）
- ErrorCode taxonomy：`src/core`（只读校验目标）
