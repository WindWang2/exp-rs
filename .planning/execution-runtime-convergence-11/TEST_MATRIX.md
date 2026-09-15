# TEST_MATRIX — 能力 → 独立 oracle → 命令 → exit → evidence

原则：每项能力的 oracle 独立于被测实现（known-answer 数值真值、暴力模拟对照、字节级对照、计数不变量），禁止测试复用被测实现制造绿灯。

| 能力 | 独立 oracle | 测试文件 | 命令（preset dev-default, build-dev） | exit | evidence |
|---|---|---|---|---|---|
| WP-A 架构唯一权威（无第二 scheduler） | 源扫描 allowlist（scheduler 形态所有权清单，独立于运行时） | tests/test_execution_authority_11.cpp | `ctest --test-dir build-dev -R execution_authority_11 -j1` | 0 | 见右栏 |
| WP-A cancel 链 JobEngine→context | 行为：flag 置位后 throwIfCancelled 抛出；任务终态 Cancelled | tests/test_execution_authority_11.cpp | 同上 | 0 | 见右栏 |
| WP-A ChunkPipeline consumer-abort fail-closed | 行为：consumer 返回 false → run() 抛 ChunkConsumerAborted（不再静默 0 退出） | tests/test_execution_authority_11.cpp | 同上 | 0 | 见右栏 |
| WP-B partition identity 稳定/漂移 | known-answer：固定输入→固定 digest；任一字段变化→digest 变化（手工向量） | tests/test_chunk_contract_11.cpp | `ctest -R chunk_contract_11 -j1` | 0 | 见右栏 |
| WP-B 错误信封全表映射 | known-answer 表：chunk 异常→RSOperatorError code 一一对照 | tests/test_chunk_contract_11.cpp | 同上 | 0 | 见右栏 |
| WP-C 子进程崩溃恢复 | kernel 执行计数器 + 输出 byte-equal 直跑基线（真值=独立全量计算） | tests/test_chunk_resume_11.cpp | `ctest -R chunk_resume_11 -j1` | 0 | 见右栏 |
| WP-C 身份漂移拒绝复用 | 改 identity 后 resume 必然全部重算（计数=totalTiles） | tests/test_chunk_resume_11.cpp | 同上 | 0 | 见右栏 |
| WP-C 损坏矩阵 fail-closed | 翻转 tile 字节/journal 行/checkpoint → 重算或类型化失败，绝不静默错数据 | tests/test_chunk_resume_11.cpp | 同上 | 0 | 见右栏 |
| WP-C exactly-once publication | PUBLISHED marker 后重入→零 kernel 调用、成功返回 | tests/test_chunk_resume_11.cpp | 同上 | 0 | 见右栏 |
| WP-D planner 保守上界 | 暴力占用模拟（独立模拟器枚举流水线交错）≤ plan 估计 | tests/test_execution_governor_11.cpp | `ctest -R execution_governor_11 -j1` | 0 | 见右栏 |
| WP-D 预算/降级/泄漏 | 超限→typed 拒绝；泄漏→DiagnosticReport+计数 | tests/test_execution_governor_11.cpp | 同上 | 0 | 见右栏 |
| WP-E lease 过期/接管/毒丸 | 注入时钟（独立时间源）状态机对照表 | tests/test_worker_lease_11.cpp | `ctest -R worker_lease_11 -j1` | 0 | 见右栏 |
| WP-F 事件上限/采样界 | 事件数 ≤ tiles/N+O(1)；ring 溢出丢弃最旧并标记；计数器=tile 数 | tests/test_execution_telemetry_11.cpp | `ctest -R execution_telemetry_11 -j1` | 0 | 见右栏 |
| WP-G adoption kit known-answer | synthetic 算子输出 vs 独立闭式公式（逐 tile 手工数学） | tests/test_chunk_adoption_11.cpp | `ctest -R chunk_adoption_11 -j1` | 0 | 见右栏 |
| WP-H 1e6 逻辑 tile 不物化 | 内存分配差值断言（O(1)）+ 计数不变量 | tests/test_execution_scale_fault_11.cpp | `ctest -R execution_scale_fault_11 -j1`（1e6 用 SICNU_SCALE_11=1 opt-in） | 0 | 见右栏 |
| WP-H 取消风暴/间歇崩溃至完成 | 多轮随机点取消/崩溃后最终 byte-equal + 恰好一次出版 | tests/test_execution_scale_fault_11.cpp | 同上 | 0 | 见右栏 |
| 回归：既有 chunk/execution 套件 | 既有断言（不修改语义者） | test_chunk_graph / test_external_memory_10 / test_large_scale_execution_10 / test_job_engine / test_task_center | `ctest -R "chunk_graph|external_memory_10|large_scale_execution_10|job_engine|task_center" -j1` | 0 | 见右栏 |
