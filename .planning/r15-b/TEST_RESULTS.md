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

## 5. 第三轮（2026-10-16 22:00–00:5x UTC+8，本机实跑）

### 5.1 RED 复现（恢复丢失的证据）
RED 工作树 `exp-rs-r15-b-red`（本轨新建的独立 worktree + `build-red`，不触碰其他 worktree）。

**原始 5 个修复**：工作树在 `07e2554b`，只把 6 个生产文件回退到 BASE `46791bc6`
（raster_reader.cpp、vector_reader.cpp、catalog_service.cpp、remote_identity_resolver.cpp、reproduction_bundle_import.cpp、experiment_store.cpp），测试保持 `07e2554b`。
`test_r15_B_geodata_persistence "B13*,B05*,B04*,B12*,B07*"` 两次：均 9 个用例 2 过 7 败（exit 42）：

| WP | 修复前观测（本机） |
|---|---|
| B13 | Raster / Vector 读者都没有拒绝混代组；kill -9 循环中 29 次断言失败 |
| B05 | searchAll 未检测到重复 rel=next（:366 失败） |
| B04 | 抛 `RemoteSourceValidator: private / loopback / link-local hosts are blocked` |
| B12 | `escape.txt`（符号链接）被迁移 |
| B07 | 损坏叶页后 list 扫描返回部分结果且成功（:644 失败） |
（2 个通过的是 B13 committed 标记、B05 正常分页这两个不变量用例。）

**本轮新修复**：工作树在 `07e2554b`（修复前）+ 拷入本轮新测试，`test_r15_B_stores` 加 `-DR15B_LEGACY_API`（只编掉新 API 检查）：

| 项 | 修复前观测 |
|---|---|
| 第 5 项 B07 尾部 | allLineageEdges 32/60、outgoingEdges 40/60、GovernanceStore allTags 40/60，均无错误信号；lineage 图无"不完整"标记 |
| 第 4 项 #1449 | DatasetStore / ExperimentStore / Governance+WorkspaceCatalog 三个 race-close 用例 **SIGSEGV** |
| B06 | 外部改写后 bandCount 仍为 1（应 3），尺寸仍 8×8（应 16×12）；写入垃圾后 state 仍为 Ready |
| B15 | 重签名 URL 的 recordedToken 为空、isStale=false；签名与 AKIA 凭据出现在 SQLite 文件里 |
| 第 7 项 CRLF | 完好的 CRLF bundle 被判 `checksum mismatch: manifest.json` |
| 第 7 项 journal 闸门扩展 | inspectRaster / inspectAny / validateCog / translateRaster 在存在 `.publish-journal` 时成功 |
| #1505 T9 | `resolveAgainst("/base","reports/100%_coverage.tif")` 为 Invalid |
| #1505 T6 | 策略拒绝后仍读到 4096 字节，fallback_reads 0→1，origin 请求 2→5 |
| #1505 T3 | 重绑定解析器：lookups=2，fixture 收到 1 个请求（连到了 127.0.0.1） |
| TSan（B09） | 本树修复前：1 条 data race（`allTags()` 写 / `stepFailed()` 读 `Impl::stepFailed`） |

首跑即绿（不构成 RED→GREEN，属回归 oracle）：B11 四个用例；#1481 T6/T7/T8/T9/T19；#1449 reopen-after-close。
开后复查（journal 闸门）没有确定性 RED（时间窗口），只由 B13 套件保持绿覆盖。

### 5.2 GREEN×2（HEAD 代码 = `f8e1f280`）
- `test_r15_B_stores | test_r15_B_partition_metadata | test_r15_B_post1505`：22/22，两次。
- 回归子集（ctest -R 模式 = light 25 个目标 + 重型 `test_governance_store test_data_governance_consistency test_workspace_catalog test_spatial_block_leakage test_stratified_split test_r12b_e2e_durability` + 本轨 4 个目标）：**211/211，两次**（24.7 s / 17.0 s）。
- `test_platform7_library`（19 用例 283 断言）、`test_experiment_evaluation`（32 用例 501 断言）：ctest 未注册它们（tolerant discovery 产出 0 条），直接运行二进制，两次均 exit 0、全部通过。
- 提交 `c68d5e95` 在精确提交上单独验证：`test_r15_B_stores` 8/8 两次。

### 5.3 Sanitizer（独立构建目录 `build-r15-b-tsan` / `build-r15-b-asan`，RelWithDebInfo -O1 -g，-j2 + flock）
| 构建 | 目标 | 结果 |
|---|---|---|
| `-fsanitize=thread` | test_r15_B_stores / post1505 / partition_metadata | 修复前 1 条真实 race（已修 f8e1f280）；修复后两次各 0 报告，全部用例通过 |
| `-fsanitize=address,undefined`（detect_leaks=1） | 同上 | 两次各 0 ASan / 0 LSan / 0 UBSan 报告，全部通过 |
注意：Qt / GDAL / SQLite 系统库未插桩，TSan 看不到其内部同步，可能漏报；相关存储测试（test_store_fault_injection 等）的 sanitizer 构建约 3600 步（拉起 qgis 依赖），**未运行**。

### 5.4 其他
- `git diff --check 46791bc6..HEAD`：通过。
- `git fetch origin master`：仍为 `46791bc6`（HEAD 的祖先），无需 rebase。

## 7. 第四轮（2026-10-17 00:4x–02:xx UTC+8，本机实跑；T11 / R3-10 / R4-01 / R4-02）
### 7.1 RED（先写测试，在未修复代码上运行）
| 用例 | 修复前观测 |
|---|---|
| 1505-T11 data cache <url> 默认拒绝回环 | 退出码 0，ok=true，fixture 收到 2 次请求 |
| 1505-T11 data identity 默认拒绝回环 | 退出码 0，ok=true，fixture 收到 1 次请求 |
| R3-10 传输失败不回落无守卫通道 | 重绑定名先 3 次解析到公网（连接失败），回落后连到 127.0.0.1：fixture 收到 3 次请求，读出 4096 B |
| R4-01 Stat 不探测私网 | `VSIStatL` 返回 0，fixture 收到 1 次请求 |
| R4-01 open 时探测受守卫并钉住 | 重绑定名 open 返回句柄，fixture 收到 1 次请求 |
| 1505-T11 prefetch 默认不访问回环资产 | 53941117 上 fixture 收到 6 次请求；R4-01 修复后仍有 2 次（规划阶段）；R4-02 修复后 0 次（opt-in 正对照 >0） |
R3-10 用例另外断言"失败原因是传输失败，不是策略拒绝"，防止解析次数标定错误时测试空转。

### 7.2 GREEN×2（代码 = `08d8b295`）
- `test_r15_B_cli_private_network` 4/4（19 个断言）×2；`test_r15_B_geodata_persistence` 的 R3-10 / R4-01 / 1505-T* 用例 ×2。
- 相关回归子集（第三轮的 211 个 + 本轮新增 7 个）：**218/218 ×2**。
- range cache / fabric / identity 扩展子集（test_cli_commands_json、test_io_range_cache、test_io_fabric_*（object_store、identity_11、locality_11、plan、operators、multidim_11、scale、replay_11）、test_io_microbench、test_portability_contract、test_io_mirror_maintenance、test_io_identity）：**107/107 ×2**。
- `test_platform7_library`（283 个断言）与 `test_experiment_evaluation`（501 个断言）直接运行 ×2，全部通过。

### 7.3 Sanitizer（最终代码，各运行两次）
| 构建 | 目标 | 结果 |
|---|---|---|
| TSan | test_r15_B_stores / post1505 / partition_metadata / geodata_persistence（18 个用例，含 R3-10、R4-01） | 0 报告，全部通过 |
| ASan+UBSan（detect_leaks=1） | 同上 | 0 ASan / 0 LSan / 0 UBSan，全部通过 |
`test_r15_B_cli_private_network` 通过 popen 驱动真实 CLI，sanitizer 只会插桩测试外壳本身，因此未做 sanitizer 构建。它覆盖的 range_cache 代码已由 geodata_persistence 的 sanitizer 运行覆盖。

### 7.4 其他
origin/master 仍为 `46791bc6`，无需 rebase；`git diff --check` 通过。

## 6. 未运行（NOT RUN）
- Windows、macOS。
- 全量 ctest（只跑了相关子集 218 条 + 扩展子集 107 条 + 两个直接运行的二进制；全量由远端 CI 覆盖）。
- 重型存储测试（test_store_fault_injection 等）的 sanitizer 构建：第四轮只要求 test_r15_B_*，因此未运行（见 5.3）。
- 远端 CI（按要求不等待）。
