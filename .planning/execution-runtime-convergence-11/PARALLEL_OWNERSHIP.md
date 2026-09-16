# PARALLEL_OWNERSHIP — open PR / branch / issue 与本 track 的文件级关系

刷新时间：2026-09-15（Phase 0）。刷新方法：`git fetch origin --prune` + `gh pr list/view/diff` + `gh issue list`。原始数据见 BASELINE.md。

## Open PR 交集矩阵

| 对端 | 独占变更域 | 与本 track primary scope 交集 | 共享注册文件 | 策略 |
|---|---|---|---|---|
| PR #1008 `zcode/radiometric-spectral-workbench`（open，DIRTY） | src/agent/spatial_tools/spectral_*、src/analysis/{atmospheric,hyperspectral}、src/app/widgets/spectral_*、src/core/{radiometric_state,spectral_library}、src/processing/algorithms/{radiometric_calibration,spectral_indices,spectral_unmixing}、docs/adr/0158、tests/test_{spectral,radiometric,continuum,fast_6s,d13}*、.planning/radiometric-spectral-workbench | **0 个文件**（src/runtime、src/jobs、src/operators/framework、tests/*execution*、tests/*chunk*、docs/execution 均不在其 diff） | `.gitignore`、`tests/CMakeLists.txt`、`src/{agent,analysis,app,core}/CMakeLists.txt`（本 track 只改 `tests/CMakeLists.txt`、`.gitignore`、`src/runtime/CMakeLists.txt`、`src/operators/framework/CMakeLists.txt`、`src/jobs/*` 如需） | #1008 文件一律 read-only、不复制其功能；共享注册文件本 track 仅 append-only 追加（新测试目标、新 .planning 白名单三行），rebase 可自动合并；不依赖其任何新 API |

规则执行情况：
1. #1008 的 changed files 默认 read-only —— 本 track PLAN 中无任何对其文件的写操作。
2. 本 track 不依赖 #991/#992 的新 API（它们已合并进 master 基线 `a5b11b7f`，属稳定 seam；对 workflow/dataset 只读消费，不写）。
3. #991/#992 已合并：`c5d4aafe`、`1cea9892` 已在基线内，无需保留旧假设。
4. 新出现的并发 PR 同样适用（Phase 5/8 rebase 前会再次 `git fetch` 复查）。
5. Open issues #1001–#1007 逐条 dedupe：均不在 execution runtime 域（io clip / workflow executor / dataset join / agent qa / georef pick / workflow tests / dataset qa CRS），无一被本 track 代码修复或重复实现 → EVIDENCE.md OUT_OF_SCOPE 登记但不修复（属 R2 review 残留，各域 track 职责；修改会跨 primary scope）。

## 本 track 写范围声明（审计后确认，未扩大）

- **独占写**：`src/runtime/**`、`src/jobs/**`、`src/operators/framework/**`、`tests/test_{chunk,execution}_*_11.cpp`（匹配 tests/*chunk*、tests/*execution* 模式）、`docs/execution/**`、`.planning/execution-runtime-convergence-11/**`。
- **最小 append-only 接线**（共享文件）：`src/runtime/CMakeLists.txt`、`src/operators/framework/CMakeLists.txt`、`tests/CMakeLists.txt`、`.gitignore`（白名单三行）、`CHANGELOG.md`（一段）。
- **最小集成修复**（非独占但属本 track 业务主体的接缝）：`src/processing/framework/fused_chain.cpp`（死 cancelFlag 接线 + 异常翻译，~30 行内；它是 ChunkPipeline 唯一生产消费者，不修则"取消传播到 chunk 执行"不可证明）。除此之外不改 src/processing、src/workflow、src/dataset、src/app。
