# R15-B 最终报告（FINAL_REPORT）

轨道 B：空间数据链路、数据集/实验存储与可恢复持久化。只审计/加固既有模块，没有新增 IO 框架、产品模块或框架迁移，也没有重做已合并的修复。

| 项 | 值 |
|---|---|
| 分支 | `hardening/r15-B-geodata-persistence` |
| BASE_SHA | `46791bc6`（origin/master，fetch 后仍为最新） |
| 代码 HEAD | `e89b9b51`（之后只有本文档提交） |
| 工作树 | `<repo 父目录>/exp-rs-r15-b-20261016`；构建树 `build-r15-b` |
| 工具 | `/teamwork-preview`、`/goal-loop`：未安装/等价执行（Oracle-Ledger 循环） |

## 1. 修复索引

| WP | 级别 | 缺陷 | 修复 | 文件 | 提交 | Oracle |
|---|---|---|---|---|---|---|
| B13 | P0（科学数据损坏） | 组发布在侧车与主文件 rename 之间被 kill，读者会打开混代组（#1481 T11 残留） | 读侧闸门 `atomic_fs::throwIfInterruptedPublish`：存在未提交 journal 即拒绝读取，无副作用；committed 标记不阻断 | atomic_fs.{h,cpp}、raster_reader.cpp、vector_reader.cpp | cec9b8d5、c85f475e | O-B13.1–4（含 40 轮 kill -9） |
| B12 | P0（安全） | bundle 导入把 data/ 下符号链接（可指向任意宿主文件）及未被校验和覆盖的文件迁移出去（#1481 T12 残留） | 只迁移被 checksums.txt 列入、位于规范化 data/ 内的普通文件；其余拒绝并告警 | reproduction_bundle_import.cpp | f840caf2 | O-B12.1 |
| B04 | P1（回归） | #1504 后 resolver 对私网源抛异常，违反"拒绝 ⇒ 空 token"契约，中断调用方指纹收集 | probe/revalidate 捕获 `GeoError` ⇒ 空 token，并照常丢弃旧 token | remote_identity_resolver.cpp、test_io_remote_validator.cpp | f6e720a2 | O-B04.1 + 历史用例 |
| B05 | P1 | 远端 rel=next 自环/成环时 `CatalogService::searchAll` 重复拉取，结果中混入重复记录 | 对发请求的续页按 method+href+body 记账，重复即抛 `InvalidMetadata` | catalog_service.cpp | 46e321d0 | O-B05.1/2 |
| B07 | P1（数据完整性） | ExperimentStore 4 个 list 扫描 + 4 个 COUNT + prune 规划在 SQLite 中途出错时返回部分结果并报成功（#1448/#1457 残留） | 循环/单步后检查 `stepFailed()` ⇒ `experiment.store_scan_failed` | experiment_store.cpp | 853af4c6（RED）、ba9487df | O-B07.1（真实损坏叶页） |

只加 oracle、不改代码：B02（full==tile==block 拼接）、B16（#1481/#1485/#1505 SSRF 与 COG offline 闸门回归）、B14（隐藏测量用例）。

## 2. WP 分类（B01–B17）
- **FIXED_HERE**：B04、B05、B07、B12、B13。
- **只加回归 oracle（FIXED_ON_MASTER / 未发现缺陷）**：B02、B16。
- **测量**：B14（PERFORMANCE.md；发现 PERF-1/PERF-2，均为 P2，未修）。
- **部分审计**：B15（resolver 失败后不重用旧 token，已由 B04 覆盖；其余未审）。
- **未审计 / LIVE_DEFERRED**：B06、B11；DatasetStore 中 10 个返回裸 QVector 的扫描及 ExperimentStore 游标/lineage 扫描（R1-08，需改 API）。
- 其余 WP 与 issue 的分类见 ISSUE_PR_REVIEW_MATRIX.md。

## 3. 测试
- 基线：89/90（#1504 回归）。
- RED→GREEN：见 TEST_RESULTS.md §2。
- GREEN×2：机器替换后在 HEAD `e89b9b51` 重新构建并运行，两次都是 113/113。
- `git diff --check`：通过。

## 4. 评审结论
Round 1（REVIEW_ROUND1.md）与 Round 2（REVIEW_ROUND2.md）都由同一代理执行，属于等价替代，不是真正独立的评审。结论：**无 P0/P1 未关闭**；2 个 P2 已修（R1-01/R1-02）；其余为残留风险（§5）。

## 5. 残留风险
- R1-03（P2）：闸门与 `GDALOpenEx` 之间存在 TOCTOU。彻底解决需要读写锁或版本目录，超出"不新增框架"约束。
- R1-04（P2）：闸门只覆盖 `RasterReader::open` / `VectorReader::open`；`inspectRaster`、数据集池、src/app 的 QGIS 图层、子数据集 URI 不受保护。
- R1-08（P2）：其余扫描仍可能把中途错误当作结束（见 §2）。
- PERF-1（P2）：upsertRun 吞吐随规模退化。PERF-2（P2）：OFFSET 分页为 O(n²)。
- LIVE：DNS rebinding 未钉住解析结果（#1485 / #1505 T3），offline_gate 保守只读未修。
- #1449：close() 与并发调用者的生命周期属于契约限制（LIVE_DEFERRED）。
- P3：R1-05/06/07/09/10/11、R2-01。

## 6. NOT VERIFIED
- #1481 T6/T7/T8/T9/T19、#1505 T9（resource_uri.cpp，保守只读，未加 oracle）。
- #1505 T6/T11（range_cache，保守只读）。
- TSan/ASan、Windows、macOS 未运行。
- `test_platform7_library`、`test_experiment_evaluation`、`test_r12b_*`、全量 ctest 未运行。
- B06、B11 未审计；B15 只部分审计。
- 机器替换后，RED 复现与 B14 性能数字未在新机器上复测（原始日志已丢失）。
- 远端 CI 未等待。

## 7. 受保护文件（未触碰）
`src/geospatial/util/offline_gate.cpp`、`range_cache.{h,cpp}`、`resource_uri.cpp`、`src/core/temporal_cube.cpp`、`src/python/**`、`src/runtime/gpu/gpu_plane.cpp`、`src/operators/runtime/**`、`src/processing/**`、`src/app/**`、GitHub Actions。`tests/CMakeLists.txt` 只追加了一个测试块。

## 8. 与 #1505 / #1509 / #1512 去重
- 三者都已在 BASE 之前合并，目前没有开放 PR（`gh pr list --state open` = `[]`），所以不存在 COVERED_BY_OPEN_PR 项。
- 没有 cherry-pick 任何提交（含 #1512）。
- #1505 已修的 SSRF 项只加回归 oracle（B16），没有改 offline_gate.cpp。
- B04 修的是 #1504 引入的回归，与 #1505 T1 的守卫开关互补（守卫保留开启，只修正拒绝的表达方式）。
- 本轨每个修复点（读侧闸门、searchAll 去环、bundle data/ 迁移、ExperimentStore 扫描、resolver）的 RED 都是在已包含 #1505/#1509/#1512 的 BASE `46791bc6` 上复现的，因此不会与这三个 PR 已合并的修复重复。
