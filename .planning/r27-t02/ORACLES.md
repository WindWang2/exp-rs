# ORACLES — R27-T2（每条 AC = 可失败命令 / 确定性输入；测试载体 `tests/test_r27_t02_*.cpp`）

| # | AC | Oracle（可复验） |
|---|---|---|
| O1 | `addJoin({A,A})` / `addJoin({A,B,A})` 在登记前抛 `std::logic_error`（typed），节点不注册（后续合法单消费者仍可挂 A）；`addJoin({})` 运行时拒绝（非 assert） | `ctest -R r27_t02_chunk_graph` 用例：重复输入抛 + 状态不变 + 空 vector 抛 |
| O2 | 合法 A+B join 输出对齐；取消（flag 中途置位）→ `ChunkGraphCancelled`；bounded queue 背压不丢不重 | 同上文件：alignment/cancel/backpressure 用例（含 queueCapacity=1 的压力路径） |
| O3 | 同 statePath 两线程并发 `execute()`：恰一个成功持锁运行，另一个抛 typed `ChunkRunLockHeld`（fail-closed），且前者产物/ journal 不被破坏；真实子进程持锁崩溃后锁自动释放，下一次 `execute()` 确定性恢复（复用已提交 tiles） | `ctest -R r27_t02_resume_lock`：双线程竞争 + fork 子进程崩溃释放 + 恢复计数 |
| O4 | `JobEngine::shutdown()` 在 worker 执行"永不返回且不查取消"的 job 时，于 deadline（默认 ≤5s，测试注入 ≤1s）内返回；detach 不产生 UAF（对象 immortal）；shutdown 后 submit 拒绝语义不变（#684） | `ctest -R r27_t02_job_engine`：stuck-worker shutdown 用例（超时断言）+ 既有 job_engine 全量回归 |
| O5 | 晚到取消：job 成功完成后才置 cancel → 终态 Succeeded（含"取消来晚"日志），不再误报 Cancelled；终态后 progress 回调不再变更记录（对齐 #1693） | 同上文件：late-cancel / progress-after-terminal 用例 |
| O6 | worker recycle N 代后 `WorkerLeaseTracker::trackedCount()` 有界（≤ maxWorkers + 少量常量），不随代数线性增长 | `ctest -R r27_t02_worker_lease`：tracker 级 + pool teardown 路径断言 |
| O7 | `safeRemoteRasterPath`：私有/环回/重定向到私有 → typed 拒绝（无任何 socket 触达）；离线模式 → typed 拒绝；合法 https COG → 返回 range-cache 前缀路径且 `RemoteRangeCache::isCachePath` 为真；403/404/不可达 → 可操作错误码 | `ctest -R r27_t02_safe_remote`：validator 级用例（本地环回 http URL 必拒）+ offline gate 用例 |
| O8 | RIO-6：`RemoteDatasetPool::acquire` 对远程 URL 经受控路径打开（缓存路径入口），egress 拒绝的 URL 不产生 GDAL 网络打开 | `ctest -R r27_t02_remote_pool` |
| O9 | RIO-7 OGR SQL 注入字面量被拒绝/转义；RIO-12 CRS 输入不触网不读盘（显式 PROJ 开关）；RIO-13 v2 键 manifest 拒绝 v1 降级；RIO-14 重复 href/offset 页触发停止；RIO-11 审计行数有界 | 各自 `r27_t02_*` 用例 |
| O10 | D：ImportSession 检查→继续/取消/恢复四状态；resume 跨进程重启幂等（dedup 命中不重复注册）；操作摘要 JSON schema 版本化、机器可读 | `ctest -R r27_t02_import_session` |
| O11 | E：tile 尾批（非整倍数）空间范围/像元总数/lineage/checksum 与整批一致；journal 撕裂/损坏 tile/注入故障路径 typed 恢复 | 尾批用例 + 既有 `test_chunk_resume_11` 全绿 + fault 注入用例 |
| O12 | 全部关键 Oracle 双跑通过（同一命令连续两次退出码 0） | TEST_RESULTS.md 记录命令 ×2 + 退出码 |

**验收红线**：无删测试 / 无 SUCCEED() 冒充 / 无关闭断言；任何平台未测项如实记 NOT_EXECUTED。
