# RUNTIME_FILE_MAP — src/runtime / src/python 符号→文件→行号映射（基线 15e5c66b5）

实测结论先行：**src/runtime 无独立 SessionPool 文件**——`rg -ln "SessionPool|session_pool" src/runtime` 仅命中
gpu_plane.{h,cpp}；"有界 session 池"的 class = `ModelSessionPool`（gpu_plane.h:96）。
trace_id.h 实际位于 `src/runtime/observability/trace_id.h`（提示词写作的 `src/runtime/trace_id.h` 不存在）。

## 一、文件分布（实测 37 文件）

| 子目录 | 文件数 | 文件 |
|---|---|---|
| chunk/ | 19 | bounded_chunk_queue.h, chunk_graph.{h,cpp}, chunk_pipeline.{h,cpp}, disk_tile_store.{h,cpp}, fsync_compat.h, memory_planner.h, multi_pass_reduction.h, resumable_tile_run.{h,cpp}, scratch_registry.{h,cpp}, tile_checkpoint.{h,cpp}, tile_run_contract.{h,cpp}, tile_spec.h |
| exec/ | 2 | execution_governor.{h,cpp} |
| gpu/ | 2 | gpu_plane.{h,cpp} |
| observability/ | 10 | diagnostic_report.{h,cpp}, execution_telemetry.{h,cpp}, fault_point.h, fault_registry.{h,cpp}, trace.{h,cpp}, trace_id.h |
| worker/ | 3 | worker_lease.{h,cpp}, worker_protocol.h |
| 根 | 1 | CMakeLists.txt（`add_library(sicnu_runtime SHARED …)` :12；PUBLIC Threads :33；SHARED 理由=故障注册表全进程单例 :5-11） |

## 二、四锚点符号映射（池/租约/manifest/trace 身份）

### gpu/gpu_plane.h（池锚点）
| 符号 | 行号 |
|---|---|
| `DeviceInfo` | :24 |
| `GpuBackend`（backend seam：enumerate/allocateVram/freeVram） | :34 |
| `ModelIdentity`（modelId/modelPath/signature） | :47 |
| `SessionRequest`（model/vramMb/deviceId/reducedVramMb 阶梯） | :54 |
| `ModelSession`（sessionId/grantedVramMb/useCount） | :66 |
| `AcquireOutcome`{Acquired, AcquiredReduced, CpuFallback, Busy, NoDevice} | :76 |
| `AcquireResult` | :86 |
| `ModelSessionPool`（class） | :96 |
| → acquireSession / releaseSession / liveSessionCount / usedVramMb / devices / evictStale | :104/:108/:111/:195/:207/:214（.cpp） |

### worker/worker_lease.h（租约锚点；纯策略对象、时钟注入）
| 符号 | 行号 |
|---|---|
| `WorkerLeaseConfig`（leaseTtl/poisonFailureThreshold/maxTakeoverRetries） | :33 |
| `WorkerHealth`{Healthy, Expired, Quarantined} | :44 |
| `WorkerLeaseTracker`（class） | :51 |
| → setClock / onLiveness / onJobStart / onJobOutcome / verdict / mayTakeover / reset / Stats | :59/:62/:66/:69/:72/:77/:80/:82 |

### worker/worker_protocol.h（worker 帧协议锚点；消费方=processing LocalWorkerHost 栈）
| 符号 | 行号 |
|---|---|
| `kWorkerProtocolVersion`="1" | :40 |
| caps：progress/cancelAck/structuredErrors/outputIdentity/heartbeat | :45-:49 |
| compactFrame / makeRunRequest / makeCancelRequest / makeShutdownRequest | :53/:60/:72/:81 |
| makeReadyFrame / makeAckFrame / makeHeartbeatFrame / makeResultFrame / makeErrorFrame | :91/:108/:123/:132/:145 |
| `parseFrame`（stackLimit=128 + isObject/isInt 门） | :162 |
| frameHasCapability / frameErrorCode / frameErrorMeansCancelled / frameOutputIdentity | :199/:211/:226/:237 |

### observability/trace_id.h（trace 身份锚点）
| 符号 | 行号 |
|---|---|
| `TraceIdGenerator` | :27 |
| `kTraceSchema`="exp.trace.v1" | :45 |
| `TraceContext` | :48 |

## 三、chunk 子域符号（19 文件）

| 文件 | 关键符号:行号 |
|---|---|
| bounded_chunk_queue.h | `BoundedChunkQueue<T>` :27（push :41 满/closed 丢弃；pop :55；tryPop :69；close :81 幂等；cancel :94；cancelled :112） |
| tile_spec.h | `TileSpec` :20；`buildTileGrid` :69（int64 加固+typed throw :93-116） |
| chunk_graph.{h,cpp} | `ChunkPartitionMismatch` h:52；`ChunkGraphCancelled` h:65；`ChunkGraph` h:70（run cpp:305 双跑检查 :309；runJoin :186 对齐/误诊窗口 :224-237；runSink :257 先计数后消费 :265；**source 空像素解引用 cpp:138**；NodeId assert-only :56/:69/:86/:93） |
| chunk_pipeline.{h,cpp} | `TilePayload` h:43；`ChunkCancelled` h:56；`ChunkConsumerAborted` h:66；`ChunkPipeline::run` cpp:51（producer :93-113 / stage :116-152 / consumer :155-196；validateBuffer :24 typed） |
| disk_tile_store.{h,cpp} | `ChunkCorruptTile` h:30；write :186 / readFileImpl :155（七重校验 :103-145）；writeFile tmp=`+".part"` :236；`BoundedWriteGate` h:71（acquire :83、**release 钳 0 :96**、RAII :102） |
| scratch_registry.{h,cpp} | `ScratchBudgetExceeded` h:41；`ScratchLease` h:60（sealDigest cpp:113 全文件进内存；finalize cpp:133；verifyDigest cpp:171）；`ScratchRegistry`（acquire cpp:306；releaseEntry cpp:371 **锁外 exchange** :373；dtor :288-299 detach；sweepStale static :405 STARTUP-ONLY） |
| tile_checkpoint.{h,cpp} | `TileCheckpointWriter`（save cpp:63 唯一 tmp pid+counter :81-83 → fsync fail-closed :100-109 → rename :110；load cpp:132 五门；remove cpp:185 前缀清扫 :192-205）；`kTileCheckpointFormatVersion`=1 h:51 |
| tile_run_contract.{h,cpp} | `TileRunPartition`（**tilesAcross/tilesDown int 算术 h:55-56**；totalTiles :59）；`tileRunPartitionDigest` cpp:35；`tileRunIdentityKey` cpp:50；`tileSpecAt` h:113（**uint64→int 截断 :117-118**）；`TileRunCancelSource` h:138 |
| resumable_tile_run.{h,cpp} | `Config` h:56；ctor cpp:47（runKey="rt-"+identityKey :57）；loadJournal cpp:71（torn 尾截断 :140-151）；appendCommit cpp:159（**空文件补头 :169-176**；fault 点 exec11.journalAppend :161）；writeMarker cpp:220（tmp 固定 `+".tmp"` :224；fault 点 exec11.marker :222）；execute cpp:262（R1-R6；ckpt 异 runKey wipe :293；committed 读回比对 :336-373；fault 点 exec11.publish :413）；abandon cpp:420；cleanupAfterPublish cpp:425 |
| memory_planner.h | saturatingMul :27/saturatingAdd :36；`planTileMemory` :153（Admit :171/ReduceConcurrency :183/Spill :196/Refuse :226） |
| multi_pass_reduction.h | reduceTiles :44 / reduceStream :66 |
| fsync_compat.h | fsyncPathCompat :25（POSIX :27-41）；fsyncPathBestEffort :73 |

## 四、exec 子域

| 文件 | 关键符号:行号 |
|---|---|
| execution_governor.h | `AdmissionRefused` :40；`ExecutionGovernor` :50（Config :53；admitOrRefuse h→cpp:58 覆盖调用方预算 :62-65；scratch() :82；writeGate() :84；hasOutstandingResources cpp:81；**lastLeakReportJson 进程级 static cpp:86-90**） |

## 五、observability 子域（10 文件）

| 文件 | 关键符号:行号 |
|---|---|
| fault_point.h | `SICNU_FAULT_POINT(name)` :24（必须位于真实失败分支 :15-19） |
| fault_registry.{h,cpp} | shouldFail cpp:45-46（armed=0 快路径）；NextN/Always/EveryNth cpp:66-91；ArmedFault RAII cpp:143-146 |
| trace.{h,cpp} / trace_id.h / execution_telemetry.{h,cpp} / diagnostic_report.{h,cpp} | 见锚点表与各自头文件（WP-G 收口时引用） |

## 六、src/python 分组（实测 33 文件）

| 组 | 文件 |
|---|---|
| 嵌入控制台/绑定（根） | qgis_python.{h,cpp}（436 行解释器生命周期）、sicnu_python_api.{h,cpp}、sicnu_python_console.{h,cpp}、sicnu_python_runner.{h,cpp} |
| 隔离 worker 通道（isolated/，WP-F 接缝） | python_worker_process.{h,cpp}（QProcess 生命周期 :11-40；ensureSignalsConnected #523 :23）、python_worker_process_pool.{h,cpp}（WorkerNode :15、PoolHealthSnapshot :26、acquire/release :46-47、handleWorkerCrash :63、failPendingRequests :66）、python_ipc_server.{h,cpp}（PendingRequest、超时）、python_plugin_host / python_plugin_adapter / app_interface_bridge / plugin_load_context / shared_memory_segment |
| SDK（sdk/exprs/*.py，8 文件） | client/models/operators/plugin/project/workflow/__init__ |
| 脚本 | scripts/worker_daemon.py |

## 七、P1-8 校验链映射（src/operators，白名单边界修正见 DECISIONS.md）

| 接缝 | 位置 |
|---|---|
| `ModelCatalog::validateManifestJson`（纯校验入口） | mc.cpp:2750-2766（**source 传空 → 消息无 manifest 路径；不查 digest**） |
| `parseManifest`（全部 markInvalid 追加，顺序=书写序） | mc.cpp:187-1199；manifest_version 身份检查 :531-563（**排在 aux 内容 :486-525 之后**）；未知键 :731-733；词汇/边界/冲突族 :736-1128 |
| `verifyArtifactLocked`（digest/checksum，仅扫描/注册路径调） | mc.cpp:1939-2045（artifact checksum 格式 :2036-2039、不符 :2040-2043） |
| `registerManifestJson` / 扫描 ensureLoadedLocked | mc.cpp:2586-2688 / :2047-2127（重名 :2086、重 id :2093、digest :2117；ensemble 成员后扫 :2140-2195 **first-wins break**） |
| `normalizedChecksum`（只剥 sha256: 前缀，无算法推断） | mc.cpp:34-41 |
| 解释器/worker_script 校验（#1334 加，**仅 acquire 时**） | python_worker_provider（PR 侧 modelInterpreterAllowed / resolveModelWorkerScript）；catalog 校验面对同一输入返回空 → **validate/execute 面不一致** |

（mc.* = src/operators/framework/model_catalog.{h,cpp}）
