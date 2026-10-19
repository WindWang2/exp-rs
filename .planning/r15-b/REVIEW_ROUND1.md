# R15-B 评审 Round 1（对抗式审查）

> **声明**：本环境无法派生独立评审子代理。本轮评审由编写代码的同一代理在实现完成后以独立轮次执行
> （重新阅读 `git diff $(git merge-base HEAD origin/master)..HEAD`，按 权限/线程/异常/数据语义/测试质量/跨平台/所有权 七个角度逐条对抗），
> **属于等价替代，不是真正独立的评审**。

审查范围：基线 `46791bc6` → `7d2ac030`（src 7 个文件 +170/-6，tests 3 个文件）。

| ID | 级别 | 位置 | 发现 | 处置 |
|---|---|---|---|---|
| R1-01 | P2 | atomic_fs.cpp `throwIfInterruptedPublish` 消息 | 未提交 journal 既可能是"被 kill 的发布"也可能是"另一线程/进程正在进行的发布"；消息只说 interrupted，调用方可能误判为永久损坏 | **已修**：消息改为"publish is in progress or was killed"，保留 `interrupted mid-publish` 子串以保持 oracle 稳定 |
| R1-02 | P2 | atomic_fs.h:117-133 | 新增声明插在 `recoverDatasetGroup` 原有文档首行之后，导致该文档行挂到错误的函数上 | **已修**：文档行移回 `recoverDatasetGroup` |
| R1-03 | P2 | raster_reader.cpp:153 / vector_reader.cpp:62 | 检查与 `GDALOpenEx` 非原子（TOCTOU）：检查之后才开始的发布若在 GDAL 读取侧车期间完成换代，仍可能读到混代。彻底解决需要读写锁/版本目录，超出"不新增框架"约束 | 不修，记入 FINAL_REPORT 残留风险（P2） |
| R1-04 | P2 | 读侧闸门覆盖面 | 只覆盖 `RasterReader::open`/`VectorReader::open`；`inspectRaster`、数据集池、QGIS 图层（src/app，E 轨）等直接 `GDALOpenEx` 的入口不受保护；子数据集 URI（`NETCDF:"x.nc":v`）的 gdalPath 不是文件路径，闸门不生效 | 不修（跨轨/需统一入口），记入残留风险 |
| R1-05 | P3 | reproduction_bundle_import.cpp 迁移循环 | 符号链接检查与 `QFile::copy` 之间存在 TOCTOU；需要对 bundle 目录有并发写权限的本地攻击者 | 记录；不修 |
| R1-06 | P3 | 同上 | 迁移被拒时 `report.ok` 仍为 true（迁移本来就是 best-effort，bundle 仍权威），只靠 warnings 表达 | 保持既有语义；记录 |
| R1-07 | P3 | catalog_service.cpp searchAll 去环 | 依赖"相同 href+body ⇒ 相同页"；有状态游标服务器（同 href、服务端推进）会被误判为环。与 StacClient::searchAll 现有规则一致 | 记录 |
| R1-08 | P2 | experiment_store.cpp | 只修了返回 `Result` 的 4 个扫描 + 4 个 COUNT；`queryEdgesLocked`、`lineageEdgePage`、`listRunsByCursor` 的非 Result/游标路径及 DatasetStore 中 10 个返回裸 QVector 的扫描仍会把中途错误当作结束 | 需改 API，LIVE_DEFERRED，列入矩阵与残留 |
| R1-09 | P3 | test_r15_B kill9 用例 | 在可能多线程的测试进程里 `fork()` 后子进程使用 malloc/ofstream；理论上若父进程其他线程持有 malloc 锁会死锁。该用例运行时无活动夹具线程；仅 POSIX | 记录；用例用固定种子、40 轮、每轮 50–4000µs |
| R1-10 | P3 | B13 栅格/矢量用例 | 手工构造 journal，耦合内部 v1 格式；真实格式由 kill9 用例覆盖 | 记录 |
| R1-11 | P3 | test_io_remote_validator.cpp | 首见用例从"返回强 ETag"放宽为"返回空 token"——强 ETag 首见能力在回环夹具下已无法测试（#1504 默认阻断私网）；死锁回归仍覆盖 | 记录（所有权：本轨历史测试，最小改动） |
| R1-12 | — | 异常 | resolver 只吞 `GeoError`；`std::bad_alloc` 等仍会逃逸——符合"类型化拒绝 ⇒ 空 token，其它异常不掩盖" | 无需处理 |
| R1-13 | — | 权限/所有权 | 未触碰 resource_uri.cpp / offline_gate.cpp / range_cache.* / src/core / processing / app / python；tests/CMakeLists.txt 仅追加 1 个块 | 通过 |

Round 1 结论：无 P0；P1 无；2 个 P2 已修（R1-01/R1-02），其余 P2/P3 作为残留风险记录。
