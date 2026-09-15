# EVIDENCE — F15 mosaic-fusion-11

## 环境与 configure

- 主机：16 cores / 64 GB RAM / Linux 6.18.50-2-lts；磁盘可用 157G。
- Worktree：`../exp-rs-mosaic-fusion-11` @ `a5b11b7f10`，分支 `zcode/mosaic-fusion-11`。
- Configure：`cmake --preset dev-default`（后台启动于 Phase 0）→ `build-dev/configure.log` 记录 exit。
- 资源上限执行：`CMAKE_BUILD_PARALLEL_LEVEL=2`，build `-j2`，test `-j1`，`QT_QPA_PLATFORM=offscreen`。
- 编译期 60s CPU/RSS/负载采样：见 PERFORMANCE.md。

## 验证记录

（每条 gate：命令 → exit → 关键输出摘要；追加式记录）

| Phase | 命令 | exit | 摘要 |
|---|---|---|---|
| 0 | `git rev-parse origin/master` | 0 | `a5b11b7f10fa010c1c060864fb427d777ba9a4aa` |
| 0 | `git worktree add ...` | 0 | 分支创建成功 |
| 0 | `git check-ignore -v .planning/mosaic-fusion-11/GOAL.md` | 0 | `.gitignore:119:.planning/*`（按 #1008 先例 `git add -f` 跟踪） |
| 0 | `cmake --preset dev-default` | 1 | 首次失败：pybind11/Catch2 FetchContent 网络克隆 TLS 抖动 |
| 0 | `cmake -DFETCHCONTENT_SOURCE_DIR_CATCH2=<main>/_deps/catch2-src -DFETCHCONTENT_SOURCE_DIR_PYBIND11=<main>/_deps/pybind11-src -S . -B build-dev` | 0 | 复用主仓库缓存源（Catch2 v3.7.1 与 pin 一致）；无网络依赖 |
| 1 | `cmake --build build-dev --target <8 个 F15 test targets> -j2` | 0 | 全部构建成功（含 qgis_core ~1054 对象全量编译） |
| 1 | `ctest -R "test_mosaic_plan::\|test_mosaic_balancing::\|test_mosaic_seamline::\|test_mosaic_blend::\|test_mosaic_quality::\|test_fusion_quality_report::\|test_mosaic_scale::\|test_quality_mosaic_operator::" -j1` | 0 | **52/52 通过**（经 4 轮 oracle 缺陷修复，见 ledger） |

## OUT_OF_SCOPE

- Issues #1001–#1007（dataset/workflow/georef/io 域 fail-open/fail-closed 缺口）：与本 track 无文件交集，不修。
- PR #1008 与 master 的 merge conflict（DIRTY）：其 track 所有者事务。
- `mosaic_dialog`/`fusion_dialog` GUI 未接入新算子（D-011，follow-up）。

## not-executed

（如有：列出条目 + 不可自动化的原因）

## 预算记录

（Phase 超出 1.5× 时记录：phase、耗时、命令、touch 文件）
