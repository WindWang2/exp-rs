# R19-01 RISK_REGISTER

| ID | 模块/符号 | 风险 | 触发源 | 现状证据 | 级别 |
|---|---|---|---|---|---|
| RK-01 | `src/data/execution_identity_bridge.cpp` / `makeRemoteInputIdentityResolver` | 首见路径锁序死锁复发（#1548）| test_io_remote_validator.cpp:374 | #1662 声称修复但 #1662 报告该测试仍红 — 待复现 | P0 |
| RK-02 | `src/geospatial/fabric/query_planner.cpp:828` | [band,y,x] 无时间维窗口 cols 越界（#1456）| test_io_fabric_multidim_11.cpp:331 | 修复代码在 master；#1662 报告仍红 — 待复现（疑 netCDF 驱动缺失自跳过 ≠ 红）| P0 |
| RK-03 | `src/geospatial/remote/range_cache.cpp` | SSRF 标志传播 / generation restart / detach shutdown / LRU 容量 / ETag 条件 GET | A03 工作包 | :292 已传播；测试矩阵覆盖面待盘点 | P1 |
| RK-04 | `src/runtime/chunk/chunk_pipeline.cpp` `validateBuffer`/one-run | 背压/取消/析构 join 顺序/重入 | A06 工作包 | 契约在；确定性调度测试缺口待盘点 | P1 |
| RK-05 | `src/operators/runtime/*` provider | OOM 降级/会话重用/双释放/artifact 身份 | A07 工作包 | #1577 已修；降级矩阵待测 | P1 |
| RK-06 | `src/dataset/dataset_store_splits.cpp` 事务 | check-then-insert 竞态复发（#1586 家族）| A05 工作包 | saveLeakageReport 已 BEGIN IMMEDIATE；同文件其余写点待审 | P1 |
| RK-07 | `src/geospatial/formats/format_profiles.cpp:586` | `zarr.json`/STAC 声明不可达 → 身份/能力误报 | #1637-2 | **确认未修**（rfind('.')+首匹配）| P1 |
| RK-08 | `src/operators/framework/rs_json_params.h` | setRange 72 处声明无运行时强制 | #1578 | **确认未修** | P1 |
| RK-09 | `src/experiment/evaluation.cpp:393-470` | boundaryFScore 重复代码漂移风险 | #1635 | 确认未修（P3）| P3 |
| RK-10 | 发布/原子性（publish guard、rename、sidecar）| 半发布/混代 sidecar | A04/A08 | tests/test_io_paths.cpp 已有 staging/publish 用例；缺口待盘点 | P1 |
| RK-11 | 公共文件 `tests/CMakeLists.txt` | 与 #1664 合并冲突 | PR 阶段 | #1664 已改该文件 — 定向 reconcile 计划已列入 | P1 |
| RK-12 | git push 通道 | https://github.com:443 直连超时 | 交付阶段 | gh API 可用；push 时重试/查代理 | P1 |

## 已验证不变量（不得回退）
- range_cache：bounded LRU、每资源 fetch mutex + generation restart（任务书种子）
- chunk_pipeline：`validateBuffer` / one-run contract
- execution_identity_bridge：缓存与网络 probe 锁域隔离（#1548 修复后形态）
- throttleWait：deadline + maxPolls 双上界（#1603 修复后形态）
- ModelSessionPool：析构持锁遍历（#1554）
- ChunkGraph::addSink：单消费者守卫含 sink 边（#1553）
- ExecutionTelemetry::record：环形就地覆写（#1605）
- FileTraceSink：thread_local ofstream 常驻（#1604）
