# ATOMIC_FS Semantic Matrix — 35/35 (WP-B core deliverable)

基线:`origin/master` `15e5c66b5`,worktree `hardening/r4-core-foundations`。
语义轴定义(合同面 `src/geospatial/util/atomic_fs.h` 头注 + 各 API 注释):
- **staged 命名**:经 `stagedPathFor`(pid+counter+mt19937_64+O_EXCL)= 一致;自造命名 = 偏差。
- **fsync 时机**:publish 前 `fsyncFile` = 一致;rename 前无数据落盘门槛 = 偏差。
- **rename-over-existing**:publish 走 `publishStagedFile` = 一致;GC/软路径走 `renameReplaceQuiet` = 一致;裸 `fs::rename`(Windows 拒替换)= 缺陷。
- **失败清理**:失败后 `discardStaged`/等效删除 = 一致;staged 残留 = 缺陷。
- **跨进程并发**:依赖 O_EXCL claim = 一致;check-then-use = 缺陷。

分类统计:**偏差(已修)11**(rows 1-8, 22, 24, 30;commit 139721982 + af6009251 + review-fix)/ **一致 18**(含引用面、链接面、注)/ **defer-#1338 6**(rows 11, 19, 21, 27 业务调用方完整只读审计 + 合同定义面 31, 32;写权限归在途 PR #1338,其 diff 自述的 Cluster A 即 rows 19/21 的 drive-by 证据)。
填表纪律:每行分类前必读调用点上下文(≥100 行);"一致"给行内证据(行号);禁止从相邻行复制结论。

| # | 调用方文件 | 使用面 API(rg 实证) | staged 命名 | fsync 时机 | rename-over-existing | 失败清理 | 分类 | 处置 |
|---|---|---|---|---|---|---|---|---|
| 1 | src/agent/cartography/export.cpp | publishStagedFile(2)@:256/:434 | QTemporaryFile(XXXXXX,O_EXCL)✓ | 偏差→已修:publish 前 fsyncFile(两处 site 均修,atlas 循环第二处由 P1-3 pin 锚定) | publishStagedFile✓(锁目标 fail-closed) | autoRemove 兜底✓ | 偏差(已修) | 139721982 |
| 2 | src/agent/cartography/export_manifest.cpp | publishStagedFile(1)@:208(回退) | QTemporaryFile✓ | 偏差→已修:flush 后、主路径 QTemporaryFile::rename 前即 fsyncFile(只修回退会漏主路径);P0-1 后经 try→fail 不逃逸 | 主 rename+回退 publishStagedFile(#1178)✓ | autoRemove;publish 抛错时仍 true→scope 清理✓ | 偏差(已修) | 139721982+review-fix |
| 3 | src/agent_loop/session_journal.cpp | 自建 writeFileAtomic×2+claimExclusiveUtf8+syncFileUtf8+syncDirectoryBestEffortUtf8(:56-131) | pid+counter+mt19937_64+O_EXCL✓(#1097 同构) | syncFileUtf8✓(rename 前) | fs::rename(POSIX replace;MSVC=MoveFileExW REPLACE)——不链 geospatial 刻意豁免(D-5) | 偏差→已修:ofstream open/write 失败路径补 fs::remove(P1-2 后 pin 分支锚定) | 偏差(已修) | 139721982 |
| 4 | src/analysis/classification/rs_classification_pipeline.cpp | publishStagedMembers(1)@:1382 | GDAL/RasterWriter 临时(同目录) | 偏差→已修:publishStagedMembers 前存在性守卫 fsyncFile×N(保留 skip-missing 语义) | publishStagedMembers(依赖前、主后)✓ | QFile::remove 主文件×3(注:PAM 元数据未用,.aux.xml 残留风险低) | 偏差(已修) | 139721982 |
| 5 | src/analysis/classification/rs_post_process.cpp | publishStagedFile(1)@:620+publishStagedGroup(1)@:739 | GDAL CreateCopy 临时 | 偏差→已修:GDALClose 后、drv->Rename 前单点 fsyncFile(覆盖主/回退双 rename);P1-1 后 fsync 失败路径亦清 staged | drv->Rename 主+publishStagedFile 回退✓ | 组路径 drv->Delete✓;单文件 publish 失败残留→已修(drv->Delete);fsync 失败残留→已修(P1-1) | 偏差(已修) | 139721982+review-fix |
| 6 | src/analysis/segmentation/rs_class_raster.cpp | publishStagedFile(1)@:275+publishStagedGroup(1)@:445+sidecarsFor(1) | GDAL/OGR 临时(同目录) | 偏差→已修:单文件 fsyncFile;组前存在性守卫 fsync 主+sidecar | publishStagedFile/publishStagedGroup(#1174 sidecars-first+.bak)✓ | removeIncompleteOutput/removeShapefileWithSidecars✓ | 偏差(已修) | 139721982 |
| 7 | src/app/editing/rs_edit_persistence.cpp | publishStagedFile(1)@:134 | writer 产物临时(同目录) | 偏差→已修:publish 前 fsyncFile | publishStagedFile✓ | removeTemp×2✓ | 偏差(已修) | 139721982 |
| 8 | src/app/georeferencer/qgsimagewarper.cpp | publishStagedFile(1)@:476 | GDAL warp 临时 | 偏差→已修:publish 前 fsyncFile | publishStagedFile✓ | publish 失败 QFile::remove✓(.aux.xml 低风险注) | 偏差(已修) | 139721982 |
| 9 | src/cli/cli_commands.cpp | writeFileAtomic(1)@:2298——唯一直调(其余经 RasterWriter 合同面转引) | stagedPathFor(合同面)✓ | 合同内 fsyncFile✓ | publishStagedFile✓ | discardStaged(.tif family 含 .aux.xml)✓ | 一致 | — |
| 10 | src/contracts/scientific_contract.cpp | 无直调(io 家族合同文本引 writeFileAtomic) | n/a | n/a | n/a | n/a | 一致(引用面,与实现一致) | — |
| 11 | src/geospatial/convert/raster_convert.cpp | stagedPathFor(3)@:164/:252/:340+discardStaged(3)+publishStagedGroup(1)@:101+fsyncFile(1)@:100 | stagedPathFor✓ | fsyncFile→publishStagedGroup✓(:100-101) | publishStagedGroup✓ | discardStaged×3✓ | 一致(write 权 defer-#1338) | defer-#1338 |
| 12 | src/geospatial/doctor/data_doctor.cpp | fileExists(2)+sidecarsFor(1)@:92-94(只读诊断) | n/a | n/a | n/a | n/a(只读) | 一致 | — |
| 13 | src/geospatial/fabric/mirror.cpp | writeFileAtomic(4)@:881/:1005/:1215/:1598+removeFileQuiet(6) | 合同内 stagedPathFor✓ | 合同内 fsync✓ | 合同内 publish✓ | 合同 discardStaged✓;removeFileQuiet×6 限锁/GC+stale 守卫(pidAlive+age)✓ | 一致 | — |
| 14 | src/geospatial/fabric/mirror.h | 声明面引用(无直调) | n/a | n/a | n/a | n/a | 一致(声明面) | — |
| 15 | src/geospatial/io/finalize_manifest.cpp | writeFileAtomic(1)@:120+fileExists(2) | 合同内 stagedPathFor✓ | 合同内 fsync✓ | 合同内 publish✓ | typed throw on open/write fail✓ | 一致 | — |
| 16 | src/geospatial/io/finalize_manifest.h | writeFileAtomic(1,声明/注释) | 同 15 | 同 15 | 同 15 | 同 15 | 一致 | — |
| 17 | src/geospatial/io/metadata_patch.cpp | fsyncFile(1)@:340 | n/a(原地补丁) | fsyncFile(path) after GDALClose✓ | n/a(无 rename) | n/a | 一致 | — |
| 18 | src/geospatial/io/stage_ledger.cpp | writeFileAtomic(1)@:56+fileExists(7)+removeFileQuiet(2)+discardStaged(2)+stagedPathFor(1)+sidecarsFor(1)+publishStagedGroup(1)+fsyncFile(1) | writeFileAtomic/stagedPathFor✓ | fsyncFile(staged)→publishStagedGroup✓(:360-361,范本) | publishStagedGroup✓ | discardStaged+sweepOrphans(形状解析)+size-cap 读✓ | 一致(范本) | — |
| 19 | src/geospatial/raster/raster_writer.cpp | stagedPathFor(1)@:117+discardStaged(4)+publishStagedGroup(1)@:410+fsyncFile(1)@:388+fileExists(1) | stagedPathFor✓ | fsyncFile(mStaged)→publishStagedGroup✓(:388-410) | publishStagedGroup✓ | discardStaged 全失败路径+析构✓ | 一致(write 权 defer-#1338) | defer-#1338 |
| 20 | src/geospatial/remote/range_cache_disk.cpp | 无直调(include 面;fileOpenUtf8 边界由 portability pin 覆盖) | n/a | n/a | n/a | n/a | 一致 | — |
| 21 | src/geospatial/vector/vector_writer.cpp | stagedPathFor(1)@:123+discardStaged(8)+publishStagedGroup(1)@:424+fsyncFile(1)@:414+fileExists(1) | stagedPathFor✓ | fsyncFile(mStaged)→publishStagedGroup✓(:414-424) | publishStagedGroup✓ | discardStaged×8 含析构路径✓ | 一致(write 权 defer-#1338) | defer-#1338 |
| 22 | src/operators/gdal/gdal_polygonize_operator.cpp | publishStagedGroup(1)@:215+fileExists(1) | OGR 组临时(workPath) | 偏差→已修:存在性守卫 fsync 主+sidecar | publishStagedGroup(#1174)✓ | removeVectorFiles✓ | 偏差(已修) | af6009251 |
| 23 | src/operators/io/io_fabric_operators.cpp | writeFileAtomic(1)@:269 | 合同内 stagedPathFor✓ | 合同内 fsync✓ | 合同内 publish✓ | 合同 discardStaged✓ | 一致 | — |
| 24 | src/operators/runtime/detection_tile_engine.cpp | publishStagedGroup(1)@:241 | GDAL 组临时(workPath) | 偏差→已修:同 22 | publishStagedGroup✓ | removeVectorFiles✓ | 偏差(已修) | af6009251 |
| 25 | src/operators/runtime/model_publish.h | 无直调(注释引用面:'.bak set' 说明) | n/a | n/a | n/a | n/a | 一致(引用面) | — |
| 26 | src/processing/framework/output_committer.cpp | 无直调(注释引用面;实际发布走 row 27) | n/a | n/a | n/a | n/a | 一致(引用面) | — |
| 27 | src/processing/gdal/staged_raster_output.h | stagedPathFor(3)@:43+publishStagedFile(3)@:68+discardStaged(3)@:48/:72+fsyncFile(2)@:67 | stagedPathFor(ctor)✓ | publish() 内 fsyncFile→publishStagedFile✓(:67-68) | publishStagedFile✓ | 析构 discardStaged+publish 失败 discardStaged✓(RAII) | 一致(write 权 defer-#1338) | defer-#1338 |
| 28 | src/workflow/artifact_gc.cpp | renameReplaceQuiet(2)@:69/:73 | .gctrash(QFile::remove 后 replace) | n/a(GC 软路径,合同豁免) | renameReplaceQuiet✓(含失败回滚 :73) | remove 失败→恢复原位✓ | 一致(软路径语义正确) | — |
| 29 | src/workflow/workflow_checkpoint.cpp | renameReplaceQuiet(1)@:412 | .orphaned 命名 | n/a(隔离软路径) | renameReplaceQuiet✓(#1186 替换,fail-closed 警告) | n/a | 一致 | — |
| 30 | src/workflow/workflow_run_coordinator.cpp | publishStagedFile(1)@:154 | QTemporaryFile/tmp | 偏差→已修:publish 前 fsyncFile | publishStagedFile(#1178)✓ | publish 失败 QFile::remove(tmp)✓ | 偏差(已修) | af6009251 |
| 31 | src/geospatial/util/atomic_fs.h(合同定义面) | — | 定义(stagedPathFor 契约) | 定义(publish 前 fsync) | 定义(ReplaceFileW→MoveFileExW/rename) | 定义(discardStaged 契约) | 定义面 | defer-#1338(写权限) |
| 32 | src/geospatial/util/atomic_fs.cpp(合同实现面) | — | O_EXCL+pid+counter+rng✓ | publish 前 fsyncFile✓ | ReplaceFileW→MoveFileExW/rename✓ EXDEV 回退✓ | writeFileAtomic catch→discardStaged✓;与 portable.h 重复面发现(PORTABILITY §4) | 定义面 | defer-#1338(写权限) |
| 33 | src/geospatial/CMakeLists.txt(链接面) | atomic_fs.cpp 入库 | n/a | n/a | n/a | n/a | 一致(链接面) | — |
| 34 | src/analysis/CMakeLists.txt(链接面) | 同上 | n/a | n/a | n/a | n/a | 一致(链接面) | — |
| 35 | src/platform/portable.h(互引注) | 头注引用 atomic_fs 模式;claimExclusiveUtf8/syncFileUtf8 同语义源 | 同源模式 | syncFileUtf8=fsync 门 | n/a | n/a | 一致(注) | — |
