# R20-02 ISSUE_TRIAGE — 126 条 open issue 的本轨分诊

规则：`open ≠ 未修`。每条结论 = issue 正文证据 vs master@28e1891390 现码实读（静态）+ 运行时回归（TEST_RESULTS.md）。
格式：`(编号, 实际路径:行, 处置)`

## P1（全部核验）
| Issue | 路径 | master 现状 | 处置 |
|---|---|---|---|
| #1535-#1540 | src/app、agent_ops、processing | 非本轨域（04/03/01 轨） | 不动 |
| #1537 STAC searchNext | src/app/stac 面板 | 非本轨（04） | CROSS_TRACK 候选 |
| #1541 CV_CN_MAX | operators/runtime/detection_tile_engine.cpp:331 | **已修**（typed refusal + 注释） | 回归保护已有（test_model_runtime 家族）；建议关 |
| #1542 ndarray 视图 | operators/python/opencv_type_caster.h:100+ | **已修**（m.u==nullptr 走 parent base） | 建议关 |
| #1543 python worker 写超时 | operators/runtime/python_worker_provider.cpp:211+ | **已修**（stopWorker+permanentlyDead） | 建议关 |
| #1544 GDAL RAII | operators/rs/rs_continuum_removal 等 | 已修（01 轨域，实读 DatasetFileGuard 在位）| CROSS_TRACK 备注 |
| #1545 SSRF 标志 | geospatial/remote/range_cache.cpp validatorOptions | **已修**（blockPrivateNetworks 传播） | 回归复跑（O2） |
| #1546 遮蔽计数 | experiment/repeat_execution.cpp | **已修**（单一 unreadableByRef） | 建议关 |
| #1547 非身份维度 | experiment/replay_deviation.cpp:125-165 | **已修**（identityDiffers 门） | 建议关 |
| #1548 冷路径死锁 | data/execution_identity_bridge.cpp:160+ | **已修**（锁外 probe） | 回归复跑 |
| #1549-#1552 | preflight/SDK/工作流 | 非本轨（03/05） | 不动 |
| #1553 addSink 守卫 | runtime/chunk/chunk_graph.cpp:44+ | **已修**（m_sinkInput 检查） | 回归复跑（chunk_graph 家族） |
| #1554 析构锁 | runtime/gpu/gpu_plane.cpp:44+ | **已修**（lock_guard + 置空） | 回归复跑 |
| #1555 nodesMutex 回调 | python/isolated/python_worker_process_pool.cpp:520+ | **已修**（锁内摘取/锁外拆除） | 建议关 |
| #1556 __shm_key__ 泄漏 | python/isolated/app_interface_bridge.cpp:43+ | **已修**（dropShmOptInFlag 全早退） | 建议关 |
| #1557/#1558 | native/测试健康度 | 非本轨 | 不动 |

## P2（本轨域全部核验，均 FIXED）
#1575（fedW halo/to_input）、#1577（mArtifact(enginePath)）、#1578（rs_param_validation）、
#1579（literal % 非逃逸）、#1580（RunId::fromString 拒绝）、#1581（provenance warnings）、
#1582（coverage 计数）、#1583（foldTotal 推导）、#1584（双侧窗检）、#1585（对称去重）、
#1586（单事务）、#1587（写失败审计）、#1603（deadline+maxPolls）、#1604（writerStream 持久）、
#1605（环形槽位覆写）。
#1595 PARTIAL（--max-items ok 未检，cli 域）→ CROSS_TRACK。**#1597 PARTIAL**（pipeline/workflow/data/catalog-export 残留 flag 黑洞，cli 域）→ CROSS_TRACK。
非本轨 P2（#1559-#1574、#1588-#1594、#1596、#1598-#1615）：不动。

## P3 技术债汇总（本轨域）
#1635-1（evaluation.cpp 重复 lambda）已修；#1637-1/-2/-3/-4 全已修（2/3 为 R19-01，1/4 后续轮）；
#1642-1/-2/-3/-4 全已修；#1647-2（dispatch 表 static）已修；#1647-1（SicnuPythonApi 未注册）在 src/python 顶层 → CROSS_TRACK。

## 结论
本轨域 swarm 层无可修项。转向 B01-B14 深度审计（见 TASKS.md 的 F1-F6）。
