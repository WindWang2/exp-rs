# R15-B 测试结果（TEST_RESULTS）

> 只记录实际运行过的命令。测试环境：`QT_QPA_PLATFORM=offscreen`，`ctest -j1`，TMPDIR / CPL_TMPDIR / CCACHE_DIR 为本轨私有目录；构建经全局 flock 锁、`-j2`。

## 0. 机器替换说明（重要）
工作中途 box 被替换：`/tmp` 下的原始日志（基线、各 WP 的 RED/GREEN 日志）丢失，整套工具链被清空。
已重装 cmake / ninja / g++ / ccache / Qt6 开发包（含 `qt6-5compat-dev`）/ GDAL 等，重新配置 `build-r15-b`，
在 HEAD `e89b9b51` 上**重新**完成目标构建并跑 GREEN×2（§3）。§1、§2 的数字来自替换前的运行输出记录，原始日志不可再提供。

## 1. 基线（master `46791bc6`，修改前）
`-R '^(test_io_uri|test_io_atomic_failures|test_fabric_catalog::test_io_fabric_catalog|test_io_stage_ledger|test_io_paths|test_io_remote_validator)::'`
→ **89/90 通过**；失败：`test_io_remote_validator::the session identity resolver returns on first sight, never deadlocks`（#1504 回归，见 BASELINE.md），由 B04 处理。

## 2. RED 证据（修复前，RED 测试提交 `b4310e60` / `853af4c6` 上观测）

| WP | 用例 | 修复前观测 | 修复后 |
|---|---|---|---|
| B13 | O-B13.1 / O-B13.3 | Raster/Vector 读者打开混代组，无异常（`refused=false`） | 抛 `IoError` "interrupted mid-publish"，恢复后可读 |
| B05 | O-B05.1 | returned=50, unique=2, requests=50（自环 next 被反复拉取直至 maxItems） | `InvalidMetadata` "pagination loop detected"，≤3 次请求 |
| B04 | O-B04.1 + 历史 resolver 用例 | 抛 `private / loopback / link-local hosts are blocked` | 空 token，0 次出站 |
| B12 | O-B12.1 | escape.txt（符号链接）与 unlisted.bin 均被迁移，relocatedFiles=2 | 二者被拒并告警，已列入校验和的文件仍迁移 |
| B07 | O-B07.1 | 损坏一个表叶页后 listRuns 返回 40/60 行且 success | 类型化失败 `experiment.store_scan_failed` |

B02、B16 的 oracle 首跑即绿（master 已修复/无缺陷），属于回归 oracle，不构成 RED→GREEN。

## 3. GREEN×2（HEAD `e89b9b51`，机器替换后重新运行，2026-10-16 21:1x UTC+8）
目标：`test_r15_B_geodata_persistence test_io_uri test_io_atomic_failures test_io_fabric_catalog test_io_stage_ledger test_io_paths test_io_remote_validator test_experiment_consistency_r4`（328 步，0 失败）。

`PAT='^(test_r15_B_geodata_persistence|test_io_uri|test_io_atomic_failures|test_fabric_catalog::test_io_fabric_catalog|test_io_stage_ledger|test_io_paths|test_io_remote_validator|test_experiment_consistency_r4)::'`

| 运行 | 结果 | 耗时 |
|---|---|---|
| 第 1 次 | **113/113 通过**（exit 0） | 8.46 s |
| 第 2 次 | **113/113 通过**（exit 0） | 7.87 s |

构成：io 系列 90 + `test_r15_B_geodata_persistence` 13（隐藏 B14 不计入）+ `test_experiment_consistency_r4` 10。
替换前在 `7d2ac030`、`c85f475e` 上也各有一次 GREEN×2 = 113/113（以及更早检查点 100/100），日志已丢失。

## 4. 其他检查
- `git diff --check 46791bc6..HEAD`：exit 0。
- `git fetch origin`：origin/master 仍为 `46791bc6`，是 HEAD 的祖先，无需 rebase，无冲突。

## 5. 未运行（NOT RUN）
- TSan / ASan / UBSan 构建。
- Windows、macOS。
- `test_platform7_library`、`test_experiment_evaluation`（依赖 qgis_core，约 3317 构建步，已中止；改用 `test_experiment_consistency_r4`）、`test_r12b_*`。
- 全量 ctest。
- 远端 CI（按要求不等待）。
- B14 隐藏测量用例在新机器上未复跑（数字见 PERFORMANCE.md，来自替换前）。
- 新机器上未重新做 RED 复现（需回退源码重建，未做）。
