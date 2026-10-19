# R10-A Issue 子项归属映射（S1b 研究产物）

- 基线：master 8a0864276（worktree exp-rs-r10-algorithms，只读研究，未改任何源码）
- 范围：#1448 #1449 #1450 #1453 #1456 #1457 #1458 #1459 #1462 #1463 #1464 #1465 #1466 #1467 #1468
  （#1447/#1454/#1455/#1460/#1461 已映射，未重做）
- 分类口径：
  - `IN_SCOPE` — 落在 R10-A 正向拥有清单内（src/analysis/{classification,segmentation,hyperspectral,georeferencing,atmospheric}、src/operators/{rs,gdal,otb,opencv,io}、src/processing/{algorithms!(temporal/temporal_workspace.cpp),providers,features,contracts}、src/runtime/chunk(仅算子数据窗口契约)、data/processing/**、对应 tests/benchmarks）
  - `IN_OPEN_PR_1475/1476/1477` — 文件被在途 PR 拥有（1475=R9-A science/data；1476=R9-C ui/teaching/test-quality；1477=R9 agent execution）
  - `OUT_OF_SCOPE` — 不在正向清单（含硬排除清单路径）
  - `UNCERTAIN` — 归属需人工裁决
- 已修判定 = 在当前 master worktree 里 grep/sed 复核（附 file:line 证据），非道听途说。

## 汇总

- IN_SCOPE 子项：**5**（其中 3 项在 master 已修、2 项仍开放）
- IN_OPEN_PR：1475 覆盖 10 子项、1476 覆盖 4 子项、1477 覆盖 5 子项
- 其余 OUT_OF_SCOPE（含全部 #1448/#1453/#1457/#1458/#1462/#1463/#1464/#1465/#1466）

---

## #1448 bug(stores): fail-open false success

| 子项 | 路径 | 分类 | 证据 | 建议动作 |
|---|---|---|---|---|
| 终态 checkpoint 保存失败被吞 + 释放所有权 | src/workflow/workflow_run_coordinator.cpp:474/:870 | OUT_OF_SCOPE | src/workflow 不在拥有清单，也不在任何在途 PR 文件表 | 无 |
| stepFailed 不可达/退出不复查（promotions/benchmarks） | src/experiment/experiment_store.cpp:2236/:2517 | OUT_OF_SCOPE | src/experiment 不在拥有清单 | 无 |
| 游标分页 mid-scan 错误截断 | src/experiment/experiment_store.cpp:1203 | OUT_OF_SCOPE | 同上 | 无 |
| checkpoint restore 无内容校验、sha256 不填 | src/teaching_admin/batch_assessment.cpp:318 | OUT_OF_SCOPE | src/teaching* 硬排除 | 无 |
| graded/unavailable 计数不一致 | src/teaching_admin/batch_assessment.cpp:453-474 | OUT_OF_SCOPE | 同上 | 无 |
| run_workflow 先建实验行后校验 | src/agent/mcp_server.cpp:2637 | OUT_OF_SCOPE | src/agent 硬排除 | 无 |

结论：**0 子项归 R10-A**，全簇为 stores/teaching/agent 域。

## #1449 bug(concurrency): destruction windows

| 子项 | 路径 | 分类 | 证据 | 建议动作 |
|---|---|---|---|---|
| dataset_store close() 无锁销毁 | src/dataset/dataset_store.cpp:298 | OUT_OF_SCOPE | src/dataset 硬排除；不在任何 PR 文件表 | 无 |
| workspace_catalog close() 同型 | src/data/workspace_catalog.cpp:206-210 | IN_OPEN_PR_1475 | workspace_catalog.cpp 在 PR1475 文件表；但 PR 标题只声明 #1456/#1450/#1457 尾巴，#1449 的 close-mutex 项未必在 diff 内 | merge 后复核；不在则跟踪新 issue |
| experiment_store close() 同型 | src/experiment/experiment_store.cpp:304 | OUT_OF_SCOPE | 不在拥有清单 | 无 |
| plugin_registry refresh 悬垂指针 | src/sdk/exprs/plugin_registry.cpp:229 | IN_OPEN_PR_1477 | plugin_registry.{cpp,h} 在 PR1477 文件表 | 无 |
| data_provider_registry find() 悬垂 | src/plugins/framework/data_provider_registry.cpp:64 | OUT_OF_SCOPE | src/plugins 不在拥有清单，无 PR 覆盖 | 无 |
| **scratch lease 释放 check-then-lock** | **src/runtime/chunk/scratch_registry.cpp:387** | **IN_SCOPE（已修）** | releaseEntry 现注释直接引用 #1449：`// #1449: the registry-liveness check and the accounting it gates must share ONE mutex`（:434-441），~ScratchRegistry 在 m_mutex 下 publish registry=nullptr；Entry.registry 为 atomic 指针（:124-126、:258 acquire load）。修复随 e7e4f77a0 落地 | 保留回归测试护栏即可 |
| ipc_channel peer 版本无锁读 | src/sdk/exprs/ipc_channel.h:132 | IN_OPEN_PR_1477 | ipc_channel.{cpp,h} 在 PR1477 文件表 | 无 |
| asset_state_provider 成员无锁 | src/science_context/asset_state_provider.h:56 | OUT_OF_SCOPE | 不在拥有清单 | 无 |

结论：1 子项归 R10-A（scratch_registry），**master 已修**。

## #1450 quality(authority): single-authority drift（重点核查）

| 子项 | 路径 | 分类 | 证据 | 建议动作 |
|---|---|---|---|---|
| checkpoint 加载用无界 reader | src/workflow/workflow_checkpoint.cpp:128/:257 | OUT_OF_SCOPE | src/workflow 不在拥有清单 | 无 |
| faultlab digest 受 LC_NUMERIC 影响 | src/faultlab/util/canonical_json.cpp:77 | OUT_OF_SCOPE | src/faultlab 硬排除 | 无 |
| **四分叉 SHA-256** | src/verify/verify_sha256.cpp；src/preflight/sha256.cpp；src/grader/grader_sha256.cpp；src/faultlab/util/sha256.cpp | **OUT_OF_SCOPE（全部四处）** | find/grep 实锚：四个目录均不在 R10-A 拥有清单（另存在 src/geospatial/util/sha256.cpp、src/lab/sha256.cpp、src/planner/sha256_util.cpp，同样不在域内） | 无——**SHA-256 分叉没有任何一分叉点落在 R10-A 路径**；若做统一哈希权威，R10-A 侧只作为消费方跟进 |
| **radiometric 词汇双轨：operator 写入侧** | **src/operators/rs/**（写 SICNU_RADIOMETRIC_STATE 共 10+ 处：rs_sar_calibrate_operator.cpp:341 "sigma0"、rs_sar_terrain_flatten_operator.cpp:285 "gamma0"、rs_sar_terrain_correction_operator.cpp:272 "gamma0"、rs_sar_backscatter_operator.cpp:314、rs_sar_texture/ratio、rs_temporal_{harmonic_breaks:459,breakpoints:410,sen_trend:343,decompose:483}） | **IN_SCOPE（大体已落地）** | :285 实锚 `dst.setMetadataItem("SICNU_RADIOMETRIC_STATE","gamma0")`；契约测试 tests/test_radiometric_vocabulary_contract.cpp 已存在（头注释引 #1450，5 个 TEST_CASE，pin 了 SAR/EO writer token，含 :341/:285/:272 引用），且引用 R10-A 头 processing/algorithms/satellite_products.h 与 sar/sar_metadata.h | 持续义务：**每个新增 radiometric writer 必须同步进该契约测试**；解析侧（radiometric_state.cpp vocabulary::lookup，:95-105 未知 token 仍降级 DigitalNumber）归 PR1475 |
| radiometric 双轨：FSM 降级侧 | src/core/radiometric_state.cpp:84 | IN_OPEN_PR_1475 | radiometric_state.{cpp,h} 在 PR1475 文件表；master 已见 vocabulary::canonicalToken（:84-88 注释引 #1450） | 无（R9-A 尾巴） |
| radiometric 双轨：生产侧精确串比较 | src/operators/runtime/eo_preflight.cpp:88 | OUT_OF_SCOPE | src/operators/runtime 不在 R10-A 拥有清单（拥有的是 operators/{rs,gdal,otb,opencv,io}；runtime 仅 model_runtime.* 被硬排除，eo_preflight 属他人域）；:87-95 实锚 calibrationState 精确比较仍在 | 归 operators/runtime 属主；R10-A 勿动 |
| suitability digest 走 QJsonDocument | src/suitability/suitability_report.cpp:66、suitability_goal.cpp:330 | IN_OPEN_PR_1475 | 两文件均在 PR1475 文件表 | 无 |
| studio 版本门 && 弱化 | src/experiment_studio/studio_export.cpp:43 | OUT_OF_SCOPE | 不在拥有清单 | 无 |

结论：四处分叉 SHA-256 **零落在 R10-A**；R10-A 唯一落点是 radiometric 词汇 operator 写入侧 + 契约测试持续维护。

## #1453 bug(app): shell cluster

| 子项 | 路径 | 分类 | 证据 | 建议动作 |
|---|---|---|---|---|
| 死代码 watchTask 订阅竞态 | src/app/shell/rs_job_runner.cpp:95 | OUT_OF_SCOPE | src/app 硬排除 | 无 |
| rs_scan_pool owner map 泄漏 | src/app/widgets/rs_scan_pool.h:68 | IN_OPEN_PR_1476 | rs_scan_pool.h 在 PR1476 文件表 | 无 |

结论：0 子项归 R10-A。

## #1456 bug(data,dataset): provenance/store cluster

| 子项 | 路径 | 分类 | 证据 | 建议动作 |
|---|---|---|---|---|
| bounds-only 样本丢 sourceAssets（P1） | src/dataset/sample_promotion.cpp:328 | OUT_OF_SCOPE | src/dataset 硬排除；sample_promotion.cpp **不在** PR1475 文件表 | P1 未被任何在途 PR 覆盖——建议在 issue 上提醒 dataset 属主 |
| removeSample 非事务 | src/dataset/dataset_store_samples.cpp:464 | IN_OPEN_PR_1475 | 文件在 PR1475 文件表 | 无 |
| saveLabelSchema check-then-insert | src/dataset/dataset_store_samples.cpp:90 | IN_OPEN_PR_1475 | 同上 | 无 |
| relocate VRT 写返回值被忽略 | src/data/data_manager.cpp:771 | IN_OPEN_PR_1475 | data_manager.cpp 在 PR1475 文件表 | 无 |
| stepRow 吞 BUSY/IO | src/dataset/dataset_store_impl.h:39 | OUT_OF_SCOPE | src/dataset 硬排除；**dataset_store_impl.h 不在** PR1475 文件表（仅 facets/samples/splits）；workspace_catalog "Stmt same" 一侧随 workspace_catalog.cpp 在 PR 内 | merge 后复核是否仍开口 |
| release-noop Q_ASSERT 线程亲和 | src/data/data_manager.cpp:1259 | IN_OPEN_PR_1475 | 同 data_manager | 无 |

结论：0 子项归 R10-A；注意 P1（sample_promotion）与 stepRow 两处无 PR 覆盖。

## #1457 bug(geospatial): egress/cache cluster

| 子项 | 路径 | 分类 | 证据 | 建议动作 |
|---|---|---|---|---|
| IPv4-mapped IPv6 绕过私网过滤（P1） | src/geospatial/stac/stac_client.cpp:426 | OUT_OF_SCOPE | src/geospatial 仅 query_planner.cpp 被硬排除，其余不在 R10-A 拥有清单，无 PR 覆盖 | 无 |
| ledger 读写方言不匹配（P1） | src/geospatial/io/stage_ledger.cpp:218 | OUT_OF_SCOPE | 同上 | 无 |
| 重定向绕过首跳 egress 检查（P1） | src/geospatial/remote/http_fetch.cpp:198 | OUT_OF_SCOPE | 同上 | 无 |
| truncated 信号被覆写 | src/geospatial/fabric/query_planner.cpp:363 | IN_OPEN_PR_1475 | query_planner.cpp 在 PR1475 文件表（硬排除名单同款） | 无 |
| blockSize 旋钮失效 | src/geospatial/fabric/mirror.cpp:1228 | OUT_OF_SCOPE | 无 PR 覆盖 | 无 |
| detached 线程回调 UAF | src/geospatial/stac/stac_client.cpp:954 | OUT_OF_SCOPE | 无 PR 覆盖 | 无 |

结论：0 子项归 R10-A；stac_client/http_fetch/stage_ledger/mirror 四处 P1/P2 无人认领。

## #1458 bug(experiment): portable bundle

| 子项 | 路径 | 分类 | 证据 | 建议动作 |
|---|---|---|---|---|
| portable 模式不搬运 payload（P1） | src/experiment/reproduction_bundle.cpp:385 | OUT_OF_SCOPE | 不在拥有清单 | 无 |
| 死变量 impossible | src/experiment/reproduction_bundle.cpp:429 | OUT_OF_SCOPE | 同上 | 无 |

## #1459 bug(workflow,runtime): cluster

| 子项 | 路径 | 分类 | 证据 | 建议动作 |
|---|---|---|---|---|
| optional 解引用 UB | src/workflow/workflow_runtime.cpp:598 | OUT_OF_SCOPE | src/workflow | 无 |
| 错误消息子串决定回退语义 | src/workflow/workflow_runtime.cpp:273 | OUT_OF_SCOPE | 同上（e7e4f77a0 提交标题含 "typed plane fallback"，master 或已修，但路径不归 R10-A） | 无 |
| coordinator 锁内 IO | src/workflow/workflow_run_coordinator.cpp:1506 | OUT_OF_SCOPE | 同上（e7e4f77a0 同波） | 无 |
| **scratch digest 整文件读入** | **src/runtime/chunk/scratch_registry.cpp:124（verifyDigest :199）** | **IN_SCOPE（已修）** | :45 注释 "verifyDigest hash arbitrarily large spills in a fixed-size buffer"、:60 "Streams @p path through the digest in 1 MiB chunks"、:168 注释直接引用 "#1459: the old whole-file read allocated the entire…"；随 e7e4f77a0 落地 | 已修；回归测试归属 R10-A |
| archive remove-then-rename | src/workflow/workflow_checkpoint.cpp:200 | OUT_OF_SCOPE | e7e4f77a0 标题含 "atomic archive swap"，master 侧已处理且路径不归 R10-A | 无 |

结论：1 子项归 R10-A（已修）。

## #1462 bug(python,plugins,platform)

| 子项 | 路径 | 分类 | 证据 | 建议动作 |
|---|---|---|---|---|
| startWorker 失败留僵尸节点（P1） | src/python/isolated/python_worker_process_pool.cpp:546 | IN_OPEN_PR_1477 | python_worker_process_pool.{cpp,h} 在 PR1477 文件表 | 无 |
| rebind 裸指针捕获 UAF | src/python/isolated/python_plugin_adapter.cpp:127 | IN_OPEN_PR_1477 | python_plugin_adapter.cpp 在 PR1477 文件表 | 无 |
| unknown-method 无回执楔死 daemon | src/python/isolated/app_interface_bridge.cpp:497 | OUT_OF_SCOPE | app_interface_bridge **不在** PR1477 文件表 | merge 后复核 |
| 重复注册可劫持 operator | src/plugins/framework/plugin_runtime_host.cpp:439 | OUT_OF_SCOPE | src/plugins 不在拥有清单，无 PR 覆盖 | 无 |
| manifestOperator 悬垂死代码 | src/plugins/framework/plugin_runtime_host.cpp:386 | OUT_OF_SCOPE | 同上 | 无 |
| GetLastError 时序错 | src/platform/durable_sidecar.cpp:153 | OUT_OF_SCOPE | 同上 | 无 |
| acquireWorker 忙等可重入 | src/python/isolated/python_worker_process_pool.cpp:150 | IN_OPEN_PR_1477 | 同 pool 文件 | 无 |

## #1463 bug(agent_ops)

全部 OUT_OF_SCOPE：session_journal.cpp:108、agentbench/trace.cpp:100、recipes/recipe_registry.cpp:71 均不在拥有清单、无 PR 覆盖。

## #1464 quality(verify,grader,faultlab)

| 子项 | 路径 | 分类 | 证据 | 建议动作 |
|---|---|---|---|---|
| recordedAtUtc 字典序比较 | src/grader/grader_matcher.cpp:100 | OUT_OF_SCOPE | src/grader | 无 |
| 声明式可选参数被忽略 | src/faultlab/fault_registry.cpp:158 | OUT_OF_SCOPE | src/faultlab 硬排除；**PR1476 只改 fault_transforms.cpp，不改 fault_registry.cpp** | merge 后复核 |

## #1465 bug(help,science_context)

全部 OUT_OF_SCOPE：broker.cpp:244、asset_state_json.cpp:157、terminology_provider.cpp:179、agent_adapter.cpp:44 均不在拥有清单。

## #1466 bug(teaching,suitability)

| 子项 | 路径 | 分类 | 证据 | 建议动作 |
|---|---|---|---|---|
| 整型 goal 字段 fail-open（P1） | src/suitability/suitability_goal.cpp:54 | IN_OPEN_PR_1475 | suitability_goal.cpp 在 PR1475 文件表 | 无 |
| Windows 无 python 回退 | src/teaching_admin/script_adapters.cpp:37 | IN_OPEN_PR_1476 | script_adapters.{cpp,h} 在 PR1476 文件表 | 无 |
| 发现启发式静默丢文件 | src/teaching_admin/batch_assessment.cpp:238 | OUT_OF_SCOPE | 不在 PR1476 文件表 | 无 |
| session 文件非原子写 | src/teaching/lab_session_state.cpp:203 | OUT_OF_SCOPE | src/teaching 硬排除，无 PR 覆盖 | 无 |
| 子进程输出无上限 | src/teaching_admin/script_adapters.cpp:97 | IN_OPEN_PR_1476 | 同 script_adapters | 无 |

## #1467 bug(core-fork): local-modification cluster

| 子项 | 路径 | 分类 | 证据 | 建议动作 |
|---|---|---|---|---|
| 场景读失败吞成 NaN（P1） | src/core/temporal_cube.cpp:369 | OUT_OF_SCOPE（master 已修） | :376-392 现为 typed catch + mSceneReadFailures 计数 + qWarning 一次，注释引 "#1467" | 无 |
| preflight GDALOpenEx 打开 provider URI（P1） | src/core/radiometric_state.cpp:101 | IN_OPEN_PR_1475 | radiometric_state.cpp 在 PR1475 文件表（master :119 已见 "#1467 provider-URI skip" 痕迹） | 无 |
| **processing 目标错位编译** | **src/processing/CMakeLists.txt:6 + :419** | **IN_SCOPE（仍存在，需协调）** | :5-9 实锚 `${CMAKE_SOURCE_DIR}/src/core/spatial_split.cpp` 编入 sicnu_processing（注释自认 "Header lives at its spec-mandated src/core/ path"）；:415-425 SICNU_TEMPORAL_TIMELINE_SOURCES 编入 src/core/temporal_cube.cpp，甚至拉入 src/app/widgets/timeline_scrubber_widget.cpp；均 tests-only 消费 | R10-A 是 processing 目标属主：把 spatial_split/temporal_cube 迁入 processing 拥有目录或拆出独立目标；文件本体在 src/core，需与 core 属主协调 |
| **buffered block folds 双实现** | **src/analysis/classification/rs_spatial_cross_validation.h:21** | **IN_SCOPE（仍存在）** | :15-24 注释实锚 "bufferedBlockFolds(nx, ny, bufferDistance) … Isolation invariant mirrors src/core/spatial_split.h"——同一隔离不变量两份实现 | 消除双轨：classification 侧复用单一 folds 实现（结合上行 CMake 迁移一并做）；防空间泄漏防御漂移 |
| plugin 接口头含 QWidget | src/core/interfaces/sicnu_plugin_interface.h:5 | OUT_OF_SCOPE | src/core | 无 |
| tStart+tCount 溢出 | src/core/temporal_cube.cpp:141 | OUT_OF_SCOPE（master 已修） | :139-145 注释引 "#1450: each bound is checked on its own"，逐项比较防溢出 | 无 |
| file:// 无处理 | src/core/qgsdatasourceresolver.cpp:57 | IN_OPEN_PR_1475 | qgsdatasourceresolver.cpp 在 PR1475 文件表 | 无 |

结论：2 子项归 R10-A（均开放）：processing 目标错位 + folds 双实现，二者天然是同一笔修。

## #1468 test(ci): cluster

| 子项 | 路径 | 分类 | 证据 | 建议动作 |
|---|---|---|---|---|
| Windows 注入 main _exit 跳过 teardown（P1） | tests/sicnu_test_main.cpp:54 | OUT_OF_SCOPE（master 已修） | :53-60 现为 "Return normally; never _exit"，注释引 #1468 并解释 atexit 链即清理路径；共享测试基建，跨 lane | 无 |
| fixture server recv 无超时挂 ctest | tests/support/mini_cog_server.h:112 | OUT_OF_SCOPE（master 已修） | :99-104 已设 SO_RCVTIMEO 30s（注释提及 join 挂起教训）；消费方为 test_execution_benchmarks/test_remote_source_cache（execution-plane/geospatial 域） | 无 |
| netcdf find_library 无守卫 | tests/CMakeLists.txt:423/:558/:596 | OUT_OF_SCOPE（master 已修） | :423-434 现 if(NOT NETCDF_LIBRARY) message(FATAL_ERROR …) 注释引 #1468；sicnu_add_io_test 系列 link Sicnu::Geospatial，属 geospatial io 测试 | 无 |
| respond() 忽略 close 参数+计费失真 | tests/support/http_range_server.cpp:646 | OUT_OF_SCOPE（master 已修） | :630-644 现由调用方 close 参数裁决截断，注释引 #1468；消费方为 geospatial io 测试 | 无 |
| test_io_fabric_catalog 重复链接 | tests/CMakeLists.txt:642 | OUT_OF_SCOPE（master 已修） | grep 实锚：test_io_fabric_catalog 仅 :615 一处注册；:647 起 test_io_fabric_operators 后无 WIN32 再链 catalog；共享 tests/CMakeLists.txt 属测试基建 | 无 |

结论：0 子项需 R10-A 动作；全簇在 master 已修（cec4dc449/78148cc00 合并波）。

---

## 最优先 5 个子项（R10-A 视角）

1. **#1467 rs_spatial_cross_validation.h:21** — buffered block folds 与 src/core/spatial_split.h 不变量双实现（隔离防御漂移风险，src/analysis/classification，开放）。
2. **#1467 src/processing/CMakeLists.txt:6/:419** — spatial_split.cpp/temporal_cube.cpp 错位编入 processing 目标（还拉 src/app widget），tests-only 消费；与上一条同一笔修，需 core 属主协调。
3. **#1450 radiometric 词汇 operator 侧** — tests/test_radiometric_vocabulary_contract.cpp 与 src/operators/rs 10+ writer 的同步义务（机制已落地，防回退）；解析侧尾巴在 PR1475，merge 后对表。
4. **#1449 scratch_registry.cpp:387** — 已修（e7e4f77a0，单 mutex 释放）；确认回归测试存在并保持绿。
5. **#1459 scratch_registry.cpp:124/:199** — 流式 digest 已修（1 MiB 分块）；同上，回归护栏归属 R10-A。
