# fix(chunk,verify,study): strict admission for tile specs, queues, resume state and numeric evidence (R35-T04)

## 原问题与影响（base 120677e6 逐项源码复现）
13项typed/边界缺陷, 全部在既有模块内修复:
1. **WP01** `TileSpec::bufferElementCount()` 无checked乘法: INT_MAX×INT_MAX×8 = 恰好2^64, 回绕到**0** — pipeline/graph/disk-decode的"乘积相等"门对digest自洽的坏几何(如bufferWidth≠width+2*halo但乘积相同)全部放行
2. **WP02** memory planner: 溢出饱和UINT64_MAX与等值预算比较误Admit; 溢出不可识别(requested/minimal/spill三处)
3. **WP03** `BoundedChunkQueue::cancel()`后待处理item仍被交付, 与头注释矛盾; 取消边界可能向业务consumer多交付tile
4. **WP04** `ChunkPipeline::run()`中途线程创建抛错→局部vector析构joinable线程→**std::terminate**
5. **WP05** `ChunkGraph` addStage/addSink/queueFor仅assert, NDEBUG下越界/负ID→m_nodes[id] UB; 空Fn未拒
6. **WP06** `DiskTileStore`读取无单tile上限(有效巨文件→bad_alloc); decode不验几何一致性; path file_size与已开stream的TOCTOU窗口
7. **WP07** resume journal: 完整换行的坏末行被当torn tail静默截断(损坏证据被擦); getline无行长/总扫描上界
8. **WP08** INT_MAX级tile计划构造期无预算拒绝(bitmap 256MiB)
9. **WP10** `tileRunIdentityFromKey` sscanf接受`"1-2-3"`与尾随junk
10. **WP13** Verifier所有numeric比较先转double: 9007199254740992 == 9007199254740993(2^53折叠), gt/lt同病; UINT64/Int64 digest域失真
11. **WP14** `composePacks`先复制排序后验总cap(超cap组合的资源浪费+无来源命名)
12. **WP16** `PromotionRequest::fromJson`错型criteria被toArray清空→all_of空集vacuous true(无规则判定eligibility)
13. **WP18** `StudyReport::fromJson` curve point/band/elapsed_ms缺失/null/字符串→toDouble()变**可信0点**(sensitivity projection直通, UI无法分辨真0)

## 已合并基线
#2147(JobEngine/TaskCenter/worker protocol/workflow coordinator/retry generation/verify_types typed规则)与#2142-2145全部转回归, 不重做。

## 本次增量(2个实现commit + 1个review修复commit)
- 生产: src/runtime/chunk/{tile_spec.h, memory_planner.h, bounded_chunk_queue.h, chunk_pipeline.cpp, chunk_graph.cpp, disk_tile_store.cpp, resumable_tile_run.*, tile_run_contract.*}; src/verify/{verify_engine.cpp, verify_pack.cpp}; src/experiment/promotion.cpp; src/study/study_export.cpp
- 新测试×5: test_r35_t04_{chunk_contracts,resume_store,verifier_evidence,study_report_parse,promotion_admission} (530断言/43用例)
- 既有fixture修复×5(意图不变): test_chunk_graph(1<<30→1<<24, 旧值int溢出UB), test_runtime_chunk_boundaries_r4(默认空spec→最小合法几何), test_execution_telemetry_11/governor_11/challenger_m1_2(补合法raster维度, R1-P0)

## 边界与接口兼容
- queue契约明确化: cancel=fail-fast(丢未交付item, 锁外析构), close=drain(不丢最后tile) — 全部调用方(pipeline/graph/fabric)已核对, graph的partition-mismatch路径有isCancelling保护不会误报
- advisory(0预算)planner合同不变; Spill仍不属于fits(); 磁盘tile/journal格式不变(96B header); 1GiB读上限与journal扫描上界为新增typed拒绝, 不影响生产tile尺寸(最大16384²×1band恰在上限内)
- StudyReport writer全量输出数值字段→合法文档round-trip不受影响; 合法`mean=0.0`原样保留
- promotion: 省略(undefined)保持既有空默认, 仅错型(含null)变typed拒绝

## 本地验证(dev-default Debug, 独立build-dev, 双resource_guard -j4/-j2)
直跑二进制(CTest的-R按二进制名会漏无前缀case名, 故以二进制exit code为准), 每套件≥2次:
- 新增5套件: chunk_contracts 98断言/16用例; resume_store 315/13; verifier_evidence 66/7; study_report_parse 33/4; promotion_admission 18/3 — 全绿
- 回归19套件全绿: chunk_graph 379/35, chunk_contract_11 63/10, chunk_resume_11 47/8(含真实子进程crash), chunk_adoption_11 435/14, execution_scale_fault_11 85断言, r15_C_chunk_admission 34/5, output_committer 113/14, mlops9_evidence 98/4, experiment_consistency_r4 298/11, study_e2e 168/1, r33_t04_verify_roundtrip 57/2, r27_t02_chunk_graph 21/9, runtime_chunk_boundaries_r4 50/7, chunked_fabric_e2e **606370**/16, execution_telemetry_11, execution_governor_11, challenger_m1_2_harness(R1后补跑)
- 环境注: 本机GCC16高负载偶发ICE(系统头/dwarf2out), -j2增量重试即过, 与本diff无关

## Review
- R1(独立agent, 读diff+源码+隔离编译复现): 7 findings(P0×1组/P1×2/P2×1/P3×3) — P0/P1/P2全部修复或前置修复, P3接受并记录(.planning/r35-t04-chunk-evidence/REVIEW_R1.md)
- R2(独立agent, 最终diff 120677e6..HEAD): 结论见REVIEW_R2.md

## 限制(诚实记录)
- WP09/11/12/17判定为既有测试已覆盖(ALREADY_COVERED, 证据: output_committer #617回滚/PAM/sidecar 14用例, EXP-1 bundle round-trip), 本PR无新增量
- WP15/19/20 NOT_EXECUTED(会话容量), 已在.planning/ACCEPTANCE.md记录
- 未做: Release构建、Windows、真实GDAL栅格E2E、可见桌面验收 — 本地BLOCKED/NOT_EXECUTED, 不以stub冒充

base=120677e6, head见PR. Refs none(库级typed边界, 无对应单issue).
