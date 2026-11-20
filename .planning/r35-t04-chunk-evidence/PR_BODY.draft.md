# fix(execution): strict chunk/resume/verifier admission — cancel-safe queues, overflow-identifiable planning, exact integer evidence (R35-T04)

## 原问题与影响
以当前master(120677e6)逐项复现的10项库级缺陷, 全部为typed/边界类:
1. TileSpec元素乘法在INT_MAX×INT_MAX×bands回绕size_t; pipeline/graph/disk-decode只验乘积不验形状 — digest自洽的坏几何可被接受(WP01/WP06)
2. 内存planner溢出饱和UINT64_MAX与等值预算比较时误Admit; 溢出不可识别(WP02)
3. BoundedChunkQueue cancel后待处理item仍被交付, 与头注释矛盾; 取消边界可能多交付业务tile(WP03)
4. ChunkPipeline线程创建中途抛错→joinable线程析构→进程terminate(WP04)
5. ChunkGraph addStage/addSink/queueFor仅assert, NDEBUG下越界ID→UB; 空Fn未拒(WP05)
6. DiskTileStore读取无单tile上限(有效巨文件→bad_alloc); path file_size与已开stream存在TOCTOU窗口(WP06)
7. Resume journal坏末行无论是否完整换行都被当torn tail静默截断(证据被擦); getline无行长/总扫描上界(WP07)
8. INT_MAX级tile计划在构造期无预算拒绝(bitmap 256MiB)(WP08)
9. tileRunIdentityFromKey接受尾随junk/短写"1-2-3"(WP10)
10. Verifier所有numeric比较先转double, 2^53以上整数折叠相等; ordering同病(WP13); composePacks先复制排序后验cap(WP14); PromotionRequest错型criteria被toArray清空→all_of空集vacuous true(WP16); StudyReport::fromJson缺失/错型数值变可信0点(WP18)

## 本次增量
- 生产代码: src/runtime/chunk/{tile_spec.h,memory_planner.h,bounded_chunk_queue.h,chunk_pipeline.cpp,chunk_graph.cpp,disk_tile_store.cpp,resumable_tile_run.*,tile_run_contract.*}; src/verify/{verify_engine.cpp,verify_pack.cpp}; src/experiment/promotion.cpp; src/study/study_export.cpp
- 5个新回归测试: test_r35_t04_{chunk_contracts,resume_store,verifier_evidence,study_report_parse,promotion_admission}

## 边界/兼容
- queue契约变更: cancel=fail-fast(丢弃未交付item), close=drain — 调用方(pipeline/graph/fabric)均已按停止边界语义核对
- advisory(0预算)planner合同不变; Spill仍不属于fits()
- 磁盘tile格式不变(96B header); journal格式不变; 1GiB读上限与journal扫描上界为新增typed拒绝
- StudyReport::fromJson从"宽进"变"typed拒绝": writer产物不受影响(writer全量输出数值)

## 验证
(待填: 命令/通过数)

## Review
R1/R2: (待填)
