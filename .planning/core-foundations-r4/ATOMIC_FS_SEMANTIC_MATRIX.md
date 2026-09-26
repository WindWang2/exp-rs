# ATOMIC_FS Semantic Matrix — 35/35 (WP-B core deliverable)

基线:`origin/master` `15e5c66b5`,worktree `hardening/r4-core-foundations`。
语义轴定义(合同面 `src/geospatial/util/atomic_fs.h` 头注 + 各 API 注释):
- **staged 命名**:经 `stagedPathFor`(pid+counter+mt19937_64+O_EXCL)= 一致;自造命名 = 偏差。
- **fsync 时机**:publish 前 `fsyncFile`(或 writer 内自 flush 后仍走 publishStagedFile 的 fsync 门槛)= 一致;rename 前无 fsync = 偏差。
- **rename-over-existing**:publish 走 `publishStagedFile`(POSIX rename / Windows ReplaceFileW→MoveFileExW 链)= 一致;裸 `fs::rename`(Windows 上拒替换)= 缺陷;GC/软路径走 `renameReplaceQuiet` = 一致(设计如此)。
- **失败清理**:writer 抛出/发布失败后 `discardStaged`(含 sidecar)= 一致;staged 残留 = 缺陷;仅删主文件 = 偏差。
- **跨进程并发**:依赖 O_EXCL claim = 一致;check-then-use = 缺陷。

分类:一致 / 偏差(修复或豁免,DECISIONS.md 对应)/ 缺陷(必须修复,附提交号)/ defer-#1338(文件属在途 PR #1338,审计照做、写权限让位)。

| # | 调用方文件 | 使用面 API(rg 实证) | staged 命名 | fsync 时机 | rename-over-existing | 失败清理 | 分类 | 处置 |
|---|---|---|---|---|---|---|---|---|
| 1 | src/agent/cartography/export.cpp | QTemporaryFile(XXXXXX,O_EXCL)+同名目录 ✓ | 偏差→已修:publish 前 fsyncFile(139721982) | publishStagedFile ✓(锁目标 fail-closed) | autoRemove 兜底+publish 失败返回 ✓ | 偏差(已修) | 139721982 | TBD |
| 2 | src/agent/cartography/export_manifest.cpp | QTemporaryFile ✓ | 偏差→已修:flush 后、主路径 rename 前即 fsyncFile(139721982;仅修 fallback 会漏主路径) | 主=QTemporaryFile::rename,fallback=publishStagedFile(#1178) ✓ | autoRemove;publish 抛错时仍 true→scope 清理 ✓ | 偏差(已修) | 139721982 | TBD |
| 3 | src/agent_loop/session_journal.cpp | pid+counter+mt19937_64+claimExclusiveUtf8(O_EXCL) ✓(#1097 同构) | syncFileUtf8 ✓(rename 前) | fs::rename(POSIX replace;MSVC=MoveFileExW REPLACE)——不链 geospatial 的刻意豁免(D-5) | 偏差→已修:ofstream open/write 失败路径补 fs::remove(139721982);sync/rename 失败路径原本已清 | 偏差(已修) | 139721982 | TBD |
| 4 | src/analysis/classification/rs_classification_pipeline.cpp | GDAL/RasterWriter 临时文件(同目录) | 偏差→已修:publishStagedMembers 前 existence-guarded fsyncFile×N(139721982,保留 skip-missing 语义) | publishStagedMembers(依赖在前,主在后) ✓ | QFile::remove 主文件×3(注:.aux.xml 残留风险低,PAM 未启用元数据面) | 偏差(已修) | 139721982 | TBD |
| 5 | src/analysis/classification/rs_post_process.cpp | GDAL CreateCopy 临时 | 偏差→已修:GDALClose 后、drv->Rename 前单点 fsyncFile(覆盖主/回退双 rename)(139721982) | drv->Rename 主+publishStagedFile 回退 ✓ | 组路径 drv->Delete ✓;单文件 publish 失败残留→已修(drv->Delete)(139721982) | 偏差(已修) | 139721982 | TBD |
| 6 | src/analysis/segmentation/rs_class_raster.cpp | GDAL/OGR 临时(同目录) | 偏差→已修:单文件 publish 前 fsyncFile;组 publish 前存在性守卫 fsync 主+sidecar(139721982) | publishStagedFile / publishStagedGroup(sidecars-first+main-last+.bak) ✓ | removeIncompleteOutput / removeShapefileWithSidecars ✓ | 偏差(已修) | 139721982 | TBD |
| 7 | src/app/editing/rs_edit_persistence.cpp | writer 产物临时(同目录) | 偏差→已修:publish 前 fsyncFile(139721982) | publishStagedFile ✓ | removeTemp×2 ✓ | 偏差(已修) | 139721982 | TBD |
| 8 | src/app/georeferencer/qgsimagewarper.cpp | GDAL warp 临时 | 偏差→已修:publish 前 fsyncFile(139721982) | publishStagedFile ✓ | publish 失败 QFile::remove(tmpOutput) ✓(注:.aux.xml 同名残留低风险) | 偏差(已修) | 139721982 | TBD |
| 9 | src/cli/cli_commands.cpp | stagedPathFor(合同面) ✓ | writeFileAtomic 内 fsyncFile ✓ | publishStagedFile ✓ | discardStaged(.tif family 含 .aux.xml) ✓ | 一致 | 139721982 | TBD |
| 10 | src/contracts/scientific_contract.cpp | n/a(无直调) | n/a | n/a | n/a | 一致(引用面:io 家族合同文本引 writeFileAtomic,与实现一致) | 139721982 | TBD |
| 11 | src/geospatial/convert/raster_convert.cpp | stagedPathFor ✓(×3) | fsyncFile(staged) 后 publishStagedGroup ✓(:100-101) | publishStagedGroup ✓ | discardStaged ×3 失败路径 ✓ | 一致(审计只读:write 权 defer-#1338) | defer-#1338 | defer-#1338 |
| 12 | src/geospatial/doctor/data_doctor.cpp | n/a | n/a | n/a | n/a(只读诊断面) | 一致(fileExists/sidecarsFor 只读) | — | TBD |
| 13 | src/geospatial/fabric/mirror.cpp | writeFileAtomic 内 stagedPathFor ✓(×4) | 合同内 fsync ✓ | 合同内 publish ✓ | 合同 discardStaged ✓;removeFileQuiet×6 限锁/GC 软路径+stale 守卫 ✓ | 一致 | — | TBD |
| 14 | src/geospatial/fabric/mirror.h | n/a(声明面) | n/a | n/a | n/a | 一致(声明面) | — | TBD |
| 15 | src/geospatial/io/finalize_manifest.cpp | writeFileAtomic ✓(:120) | 合同内 fsync ✓ | 合同内 publish ✓ | typed throw on open/write fail ✓ | 一致 | — | TBD |
| 16 | src/geospatial/io/finalize_manifest.h | 同 15(头文件文档面) | 同 15 | 同 15 | 同 15 | 一致 | — | TBD |
| 17 | src/geospatial/io/metadata_patch.cpp | n/a(原地补丁) | fsyncFile(path) after GDALClose ✓(:340) | n/a(无 rename) | n/a | 一致 | — | TBD |
| 18 | src/geospatial/io/stage_ledger.cpp | writeFileAtomic+stagedPathFor ✓ | fsyncFile(staged)→publishStagedGroup ✓(:360-361,范本模式) | publishStagedGroup ✓ | discardStaged+sweepOrphans(形状解析)+size-cap 读 ✓ | 一致(范本) | — | TBD |
| 19 | src/geospatial/raster/raster_writer.cpp | stagedPathFor ✓(:117) | fsyncFile(mStaged)→publishStagedGroup ✓(:388-410) | publishStagedGroup ✓ | discardStaged 全失败路径+析构 ✓ | 一致(write 权 defer-#1338) | defer-#1338 | defer-#1338 |
| 20 | src/geospatial/remote/range_cache_disk.cpp | n/a | n/a | n/a | n/a | 一致(include 面;fileOpenUtf8 边界由 portability pin 覆盖) | — | TBD |
| 21 | src/geospatial/vector/vector_writer.cpp | stagedPathFor ✓(:123) | fsyncFile(mStaged)→publishStagedGroup ✓(:414-424) | publishStagedGroup ✓ | discardStaged ×8 含析构路径 ✓ | 一致(write 权 defer-#1338) | defer-#1338 | defer-#1338 |
| 22 | src/operators/gdal/gdal_polygonize_operator.cpp | OGR 组临时(workPath) | 偏差→已修:存在性守卫 fsync 主+sidecar(af6009251) | publishStagedGroup ✓(#1174 注) | removeVectorFiles ✓ | 偏差(已修) | af6009251 | TBD |
| 23 | src/operators/io/io_fabric_operators.cpp | stagedPathFor(合同面) ✓ | 合同内 fsync ✓ | 合同内 publish ✓ | 合同 discardStaged ✓ | 一致 | — | TBD |
| 24 | src/operators/runtime/detection_tile_engine.cpp | GDAL 组临时(workPath) | 偏差→已修:同 22(af6009251) | publishStagedGroup ✓ | removeVectorFiles ✓ | 偏差(已修) | af6009251 | TBD |
| 25 | src/operators/runtime/model_publish.h | n/a | n/a | n/a | n/a | 一致(注释引用面:'.bak set' 说明) | — | TBD |
| 26 | src/processing/framework/output_committer.cpp | n/a | n/a | n/a | n/a | 一致(注释引用面;实际发布走 staged_raster_output) | — | TBD |
| 27 | src/processing/gdal/staged_raster_output.h | stagedPathFor(ctor) ✓ | publish() 内 fsyncFile→publishStagedFile ✓(:67-68) | publishStagedFile ✓ | 析构 discardStaged + publish 失败 discardStaged ✓(RAII) | 一致(write 权 defer-#1338) | defer-#1338 | defer-#1338 |
| 28 | src/workflow/artifact_gc.cpp | .gctrash 命名(QFile::remove 后 renameReplaceQuiet) | n/a(GC 软路径,合同豁免) | renameReplaceQuiet ✓(含回滚 :73) | remove 失败→恢复原位 ✓ | 一致(软路径语义正确) | — | TBD |
| 29 | src/workflow/workflow_checkpoint.cpp | .orphaned 命名 | n/a(隔离软路径) | renameReplaceQuiet ✓(#1186 替换语义,fail-closed 警告) | n/a | 一致 | — | TBD |
| 30 | src/workflow/workflow_run_coordinator.cpp | QTemporaryFile/tmp | 偏差→已修:publish 前 fsyncFile(af6009251) | publishStagedFile ✓(#1178) | publish 失败 QFile::remove(tmp) ✓ | 偏差(已修) | af6009251 | TBD |
| 31 | src/geospatial/util/atomic_fs.h(合同定义面) | — | 定义 | 定义 | 定义 | 定义 | 定义面 | defer-#1338(写权限) |
| 32 | src/geospatial/util/atomic_fs.cpp(合同实现面) | — | O_EXCL+pid+counter+rng ✓ | publish 前 fsyncFile ✓ | ReplaceFileW→MoveFileExW / rename ✓ EXDEV 回退 ✓ | writeFileAtomic catch→discardStaged ✓ | 定义面 | defer-#1338(写权限) |
| 33 | src/geospatial/CMakeLists.txt(链接面) | atomic_fs.cpp 入库 | n/a | n/a | n/a | n/a | 一致(链接面) | — |
| 34 | src/analysis/CMakeLists.txt(链接面) | 同上 | n/a | n/a | n/a | n/a | 一致(链接面) | — |
| 35 | src/platform/portable.h(互为引用注) | 头注引用 atomic_fs.cpp 既有模式;claimExclusiveUtf8/syncFileUtf8 与 atomic_fs 同语义 | 同源模式 | syncFileUtf8=fsync 门 | n/a | n/a | 一致(注) | — |

填表纪律:每行分类前必须读过该调用方实际调用点上下文(≥100 行);"一致"也须给出行内证据(行号);禁止从相邻行复制结论。
