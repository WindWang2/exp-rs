# R24-B REVIEW_R1 — 独立评审（第 1 轮）

- Reviewer：fresh-context general-purpose 子代理（非实现者），全量 diff `5fe642ec6..HEAD` 逐文件阅读 + 13 个源 TU 与 8 个测试 TU 的 MSVC /Zs 语法级编译（21 个全 clean）。
- 并行 tile_engine 对抗审查代理（独立上下文）复查 tile_inference_engine.cpp 全文。

## 发现与处置

| 级别 | 发现 | 处置 |
|---|---|---|
| **P0** | `reproduction_bundle_import.cpp` 重挂用 `report.runId` 作改写源——该字段在 step 7 之后才赋值，重挂必然拿到空 id，全部边被 invalid_argument 拒绝（功能 DoA；4 处新测试断言会失败） | ✅ 已修：改用 `run.runId()`（commit 1e021d3fd） |
| **P1** | tile 引擎（单/多输入）：末 tile 全 NoData 时有效尾批被 `continue` 跳过 batch-full 检查且无循环后 flush——最后一条有效行/列静默丢失（Labels 产品错标 0 类） | ✅ 已修：两 lane 循环后无条件 flush（commit 1e021d3fd） |
| **P2** | `sweepStaleStagePaths` 在 Unix 匹配不到隐藏 `.<leaf>.*.tmp~` 族（QDir 需 Hidden 标志） | ✅ 已修：`QDir::Files \| QDir::Hidden` |
| **P2** | `unbucketedCount` 在空间检查未运行时上报 0（文档语义 -1=未运行），会 overstating | ✅ 已修：仅网格运行时赋值 |
| **P2** | 跨主机 QLockFile 永不收养（NAS 上崩溃进程永久封锁工件） | ✅ 已修：staleLockTime=6h 作为不可验证宿主的兜底（本地存活 pid 永不被抢） |
| **P2** | `publishClassificationArtifact` 仍用固定 `.stage~` 名在栅栏外分阶段写（与 R24-B 修复的病同形） | ✅ 已修：uniqueStagePath + sweep |
| **P2** | OOM 分类针词缺 MSVC `std::bad_alloc::what()`="bad allocation"——Windows 上主机侧 OOM 走 Unknown，OOM 阶梯失效 | ✅ 已修：补针词 |
| P3 | addSamples 在 ROLLBACK 之后取错误文本（诊断读 "not an error"） | ✅ 已修：先取后回滚 |
| P3 | 无方向无 other_* 的垃圾项被计为 legacy | ✅ 已修：legacy 判定加 `contains("other_kind")`，垃圾走 refused 路径 |
| P3 | versionStatusLocked 读失败与不存在混同（fail-closed，仅诊断质量） | 📋 记录不修（既有语义，改动影响面大） |
| P3 | 重挂远端点不做存在性检查（id-addressed loose refs 设计使然） | 📋 记录：设计决定（lineage.h 松引用），importer 已做形状/词表/字符集校验 |
| P3 | ensemble 成员 prov sidecar / publish.lock 崩溃残留不被 sweep | 📋 记录（功能性自愈，仅垃圾文件） |
| P3 | 测试 RED 性 notes：uri_percent/metamorphic 为特征固定（非 RED）；txn_gate case 2 无法从外部区分新旧（钉合同） | 📋 已在 TEST_RESULTS 注明 |

## Reviewer 判定为 SOUND 的面（供 R2 复核）

事务重排全量（六写路径 rollback 完备、无死锁、busy_timeout 交互、staged=1 WHERE 加固、fault-point 路径保持）；open() 三态门；检测哨兵与 raster lane **逐位等价**（含 BIP 布局、float 精确比较、0.0 哨兵同处理）；导出/胶囊 (kind,id) 方向判定；导入校验核（词表/字符集/自环/唯一 run 端点/外 run 拒绝/路由/256 上限）；uniqueStagePath/守卫重构（unique_ptr 首成员=最后释放）；eo_preflight 告警位置与 fed-bands 回退；WAVELENGTH_UNITS 因子与 µ 两种 Unicode；bucketOf 护栏；workspace_service 计数；CMake 追加块无语法错误且可并轨合并。

## 结论

P0/P1 全闭，P2 全闭，可进入构建验证与 R2。
