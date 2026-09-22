# Workflow 组件现状矩阵（hardening recon）

- 工作区：`/home/kevin/projects/rs-studio/exp-rs-worktrees/hardening-workflow-cri`（origin/master a9dc33fa73，只读调查，未改任何源码/测试）
- 范围：`src/workflow/` 全部 25 对 .cpp/.h（11,322 行）+ tests/ 下 14 个指定测试（8,133 行）+ 2 个额外相关测试（test_ir2_port_param_mapping.cpp、test_fault_injection.cpp）
- 证据格式 `file:line`，文件均在 `src/workflow/`（tests 在 `tests/`），未标注目录者同前缀。
- 标记：[FACT]=代码直接证明；[LIKELY]=强证据未完全验证；[UNKNOWN]=需后续调查。

---

## Executive Summary

1. 存在两套并行且格式互不兼容的 checkpoint 生态：Engine-2（`WorkflowCheckpointManager`/`WorkflowRunCoordinator`，jsoncpp，`version:1|2`，中央目录 `~/.rs_studio/checkpoints`）与 D17（`PipelineRunCoordinator`，QJson，`kind:d17_pipeline_checkpoint version:1.0|1.1`，run 目录内）。两者各自原子写、各自恢复，共享的只有 `kMaxCheckpointDocumentBytes` 和 runId 语法。
2. 原子写纪律整体很好（tmp+fsync+rename+dirfsync，双端一致），但有不对称：**D17 写侧有 16MiB cap（pipeline_run_coordinator.cpp:364），Engine-2 写侧没有（workflow_checkpoint.cpp:74-88）** —— Engine-2 可写出自己 loader 拒收的 checkpoint。
3. **D17 resume 无跨进程所有权锁**（Engine-2 有 `WorkflowRunLock`，D17 全文件无任何 lock 引用）：两个进程同时对同一 `checkpoint_<runId>.json` resumeFromCheckpoint 都会成功，非缓存后缀被双重执行。这是五方向里最大的结构性缺口。
4. D17 affinity 线程模型 + 两阶段 cancel（#1158）+ marshal-fail-drop 纪律（#1186/D7）是全仓最严密的线程设计；唯一裂缝是 whole-file hash 中的 `processEvents` 泵（pipeline_run_coordinator.cpp:163-176）打开的重入窗口 —— 外来线程析构落在泵内时可产生 UAF 窗口（现有 foreign-destruction 测试未覆盖该窗口）。
5. 路径 confinement 有 **三处独立实现**（ir2 executor / D17 isContainedInDirectory / artifact_gc isPathUnderAnyRoot），大小写策略不一致（仅 D17 在 Win/macOS 做 CaseInsensitive）；Engine-2 resume gate 对 outputLayerPath **完全没有 containment 要求**（靠 digest 完整性锚定，属威胁模型差异而非缺陷）。
6. 资源上界：唯一定量上限文件 `workflow_limits.h` 只有一个常量。节点/边数无上限；多个图算法是 O(V·E)~O(V²E)（lineage 签名定点扫描、frontier 定点排空、provenance 构建）；D17 每节点完成全量重写 checkpoint → 总写盘 O(V²)。100 节点有 scale 测试，更大规模无任何护栏。
7. 错误模型四种并存：`Result<T>`（IR2/composer/provenance）、bool+outError（两个 coordinator）、异常（workflow_runtime）、**静默忽略的 void setter**（`WorkflowRun::setAttempt/setResumeOf`）。对 hardening 而言最危险的是后两类静默路径。

---

## 现状矩阵（按组件）

### 1. WorkflowIR（workflow_ir_v2.h/.cpp，169+515 行）
| 维度 | 结论 |
|---|---|
| 权威数据源 | `WorkflowDocument`（QVector<NodeFact/EdgeFact> + version/metadata），画布/优化器/运行协调器的单一文档源（workflow_ir_v2.h:107-125） |
| 主要调用者 | 被调：WorkflowComposer::expandSubflows（workflow_composer.cpp:120）、PipelineRunCoordinator resume（pipeline_run_coordinator.cpp:1124）、e2e 测试。调用：WorkflowDagAnalyzer、WorkflowPlanOptimizer、ProvenanceGraph::fromRunState |
| 错误模型 | `Result<T>`，无异常跨缝（workflow_ir_v2.h:36-53）；fail-closed：未知/缺失 version 拒（.cpp:186-190）、缺 nodeId/operatorId/params/canvasPosition 拒（.cpp:213-234）、port 字段逐个 fail-closed（.cpp:47-84）、edge 五字段必填（.cpp:271-274） |
| 资源上界 | 无节点/边数上限；port bandCount/resolution 无范围检查（.cpp:75-78, 34-45）；嵌套深度由 Qt JSON 解析器兜底 |
| 测试覆盖 | tests/test_workflow_ir_v2.cpp 16 case（golden round-trip、fail-closed 拒收、单源不变量、V1 迁移、2.1 additive、未知顶层键存活 425）；fuzz：test_workflow_checkpoint_cache.cpp:1641 |
| 历史修复痕迹 | 头注释 pinned contract（.h:11-20）；“fresh error buffer per port”（.cpp:49-50）；#1077 port-sensitive 签名注释（plan_optimizer.cpp:53） |
| 剩余疑点 | S-1 unknown field 静默忽略（见五方向 1）；S-2 `fromJson` 不查重复 id/边、不自环 —— 语义全靠调用方再调 `validateSemantics`（.cpp:184-279 vs 323-386），目前两处生产调用方都补了调，新增调用方易漏（P2 流程性） |

### 2. ir2_port_param_mapping（133 行）
| 维度 | 结论 |
|---|---|
| 权威数据源 | 纯函数，无状态；输入 `inputArtifacts`（target port → path） |
| 主要调用者 | ir2_registry_node_executor.cpp:153（唯一生产调用） |
| 错误模型 | void，silent-by-design：永不覆盖显式 params（setParamIfEmpty .cpp:17-24）；legacy key 按排序 zip 到声明序端口（.cpp:61-95，头文件明示 documented limitation） |
| 资源上界 | 无（输入规模受上游限制） |
| 测试覆盖 | tests/test_ir2_port_param_mapping.cpp 5 case（显式端口名、不覆盖显式参数、别名、legacy zip、unbound 前缀） |
| 剩余疑点 | S-3 mixed 情形（部分 key 命中端口 + 部分 legacy）的 zip 分支（.cpp:81-95）无直接测试；legacy zip 的绑定结果依赖 key 排序 × 声明顺序，语义脆弱但已文档化（P3） |

### 3. ir2_registry_node_executor（288 行）
| 维度 | 结论 |
|---|---|
| 权威数据源 | 无状态闭包；RSOperatorRegistry 是 operator 绑定权威 |
| 主要调用者 | PipelineRunCoordinator（经 setExecutor 注入）；unbound 拒绝先于 mapping（.cpp:124-127） |
| 错误模型 | fail-closed 全链：`ir2.operator_unbound:` / `ir2.operator_failed:` 前缀（ir2_registry_node_executor.h:23, .cpp:132-140, 185-193, 240-248, 259-263）；RSOperatorError/std::exception 都转 failure（.cpp:269-284）。唯一 silent fallback：`qJsonObjectToJsonCpp` 解析失败返回空对象（.cpp:28-40） |
| 资源上界 | 无；run 目录 mkpath 不设限 |
| 测试覆盖 | test_ir2_port_param_mapping.cpp:299/336/354/375/476（缺产物 fail-closed、无 executor 拒绝、端到端 unbound、confinement、#1152 cancel 注入） |
| 历史修复痕迹 | #1032（isSafeNodeId .cpp:61-67、confinement .cpp:161-193、existence-is-not-authorship stale 删除 .cpp:197-220）、#1002（产物必须是 run 内普通文件 .cpp:249-264）、#1152（cancel flag 进 operator context .cpp:222-229） |
| 剩余疑点 | S-4 声明输出路径检查用 cleanPath 不 canonicalize（.cpp:173-174）——runRoot 内符号链接指向外部可过声明检查，但发布时 canonical 检查（.cpp:253-257）兜底拒收；净效果 fail-closed，仅报错时机靠后（P3）。S-5 qJsonObjectToJsonCpp 静默空对象（P2，见疑点清单） |

### 4. WorkflowComposer（329 行）
| 维度 | 结论 |
|---|---|
| 权威数据源 | 纯函数：fragment 内嵌于 `parameters.fragment` |
| 主要调用者 | pipeline_run_coordinator.cpp:602-612（startRun 展开后二次 validateSemantics） |
| 错误模型 | 全 fail-closed，错误以 `ir2.subflow:` + instance nodeId 命名（workflow_composer.cpp:24-27）；文件路径 fragment 拒（.cpp:111-115）；合并文档再过 validateSemantics + DAG（.cpp:300-310，注释明示不依赖调用方 revalidate） |
| 资源上界 | `kMaxSubflowDepth = 8`（workflow_composer.h，.cpp:269-272）；展开体积无上限（嵌套 fragment 参数体积受 JSON 上限间接约束） |
| 测试覆盖 | test_workflow_composition.cpp 12 case（恒等、命名空间、确定性、bindings、2.0→2.1、嵌套、深度帽、失败命名、双实例隔离、双端口映射拒、plan signature 顺序不变量） |
| 剩余疑点 | S-6 fragment 展开是文档体积放大器（8 层 × 每层任意节点），无展开后节点总数上限；O(V²E) 校验随之放大（P2，归入方向 5） |

### 5. WorkflowDagAnalyzer（177 行）
| 维度 | 结论 |
|---|---|
| 权威数据源 | 纯函数；adjacency 从文档边表构建（.cpp:13-30，确定性排序） |
| 主要调用者 | pipeline_run_coordinator.cpp:614/1139、composer .cpp:306、cost_estimator |
| 错误模型 | bool + 出参 cyclePath；环返回 isAcyclic=false 带路径（.cpp:157-169）；自环在此被检出（back edge） |
| 资源上界 | 迭代 DFS 显式栈防深递归（.cpp:49-101）；O(V+E) |
| 测试覆盖 | test_d17_workflow_pipeline_e2e.cpp:270-310（100 节点 10 层结构断言）、composer 测试间接覆盖 |
| 剩余疑点 | 无（该组件是五方向里最干净的） |

### 6. WorkflowPlanOptimizer（277 行）
| 维度 | 结论 |
|---|---|
| 权威数据源 | 纯函数；lineage 签名 = SHA-256(operator+params+入边 port/src/parentSig)（.cpp:44-81） |
| 主要调用者 | pipeline_run_coordinator.cpp:635（startRun）、:1161（resume 重算签名）、:980（planSignature→provenance） |
| 错误模型 | 环输入定点扫描 fail-safe 不旋转（.cpp:121-122）；签名缺失节点不合并（.cpp:204-212） |
| 资源上界 | 无节点数上限；**复杂度 O(V²E) 最坏**（parentsOf O(E)/节点 × 定点扫描 V 轮，.cpp:89-123）；CSE/重定向边折叠只作用于被合并端点（.cpp:239-261 注释） |
| 测试覆盖 | test_workflow_composition.cpp:423/441（签名顺序不变量、编辑敏感性）、checkpoint_cache:1072（参数变化只失效下游子图） |
| 剩余疑点 | S-7 深链（V 层链）时 startRun/resume 各做一次 O(V²E) 全量签名重算，无增量/缓存（P2 性能，归方向 5） |

### 7. contract_checker / repair_engine / cost_estimator / gate（辅助分析）
- contract_checker：纯检查 O(E)（.cpp:53-117），wildcard `*` 跳过，radiometric 升级链只查升级（.cpp:14-23, 101-107）。无疑点。
- repair_engine：闭式规则表生成 adapter 链（.cpp:80-117）；applyRepairPlan 确定性 adapter id（.cpp:131-162）。adapter 链未回灌 validateSemantics 的调用方责任（[UNKNOWN] 调用方在 src/workflow 外）。
- cost_estimator：解析模型；host RAM 探测 Linux sysinfo 否则 8GiB 默认（.cpp:14-27）；`tierWorkingSetBytes` 按 tier 累加（.cpp:29-48），O(V·tier)。bandCount 取 `outputPorts.first()`（.cpp:46-47）——多输出节点只看第一个端口，注释未说明（P3）。
- workflow_gate：闭式谓词 hasArtifact/paramNonEmpty，未知 require fail-closed（.cpp:evaluateOne）。无中文编码问题（源码内中文 hint）。无疑点。

### 8. workflow_types / workflow_definition（82+348 行）
| 维度 | 结论 |
|---|---|
| 权威数据源 | `WorkflowDefinition`（std::string + jsoncpp），Engine-2 侧文档源 |
| 主要调用者 | WorkflowRun::createFromDefinition、workflow_run_coordinator start/resume、TaskCenter::submitPipeline |
| 错误模型 | workflowDefinitionFromJson：类型先验后 cast（#1038 注释 .cpp:174-176）；kind/host 越界拒（.cpp:100-109, 136-145）；重复 step id 拒（.cpp:126-131）——**但空 id 完全跳过查重**（.cpp:124 `if (!step.id.empty())`） |
| 资源上界 | 无 step 数上限；params 深度受解析器 stackLimit 约束（MCP 入口 64：workflow_run_coordinator.cpp:461-463；checkpoint 读入路径用 jsoncpp 默认 ~1000：workflow_checkpoint.cpp:152-156） |
| 测试覆盖 | test_workflow_engine_v2.cpp（roundtrip、invalid payload）、test_workflow_runtime.cpp 内建模板、test_pipeline_runner.cpp（schema 校验） |
| 剩余疑点 | S-8 **空 stepId 可重复存在**（workflow_definition.cpp:121-132）→ createFromDefinition 产出多个空 stepId 的 plan → checkpoint 写入后 `WorkflowRun::fromJson` 以 "duplicate stepPlans id ''" 拒收（workflow_run.cpp:769-772）→ 该 run 永久不可恢复（fail-closed 但自造毒 checkpoint）（P2，见疑点清单） |

### 9. WorkflowRun（workflow_run.h 208 + .cpp 790 行）
| 维度 | 结论 |
|---|---|
| 权威数据源 | Engine-2 run 聚合：state/stepPlans/artifacts/lineage(attempt, resumeOf)，内部 mutex 串行化除 `definition()`/`findStepPlan()` 的一切（.h:103-114） |
| 主要调用者 | WorkflowCheckpointManager（save/load）、WorkflowRunCoordinator（fold/resume/swap/cancel） |
| 错误模型 | transitionTo 查状态机（.cpp:390-399）；forceSetState 逃生舱（.cpp:401-406）；fromJson 拒未知 state（.cpp:720-727）、拒非法 kind/attempt/resumeOf（.cpp:686-716）、拒未知 step status 与重复 plan id（#697/#702.5 .cpp:746-776）。**setAttempt/setResumeOf 非法值静默忽略（void 返回）**（.cpp:360-367, 375-382） |
| 资源上界 | attempt ≤ 1e6（.h:51, .cpp:695-699）；artifacts/resultPayload/params 无界 → checkpoint 体积无写侧界 |
| 测试覆盖 | test_workflow_engine_v2.cpp 7 case（状态机、创建、JSON roundtrip、runId 唯一性/安全性、invalid payload、#697） |
| 历史修复痕迹 | #750 完成身份字段（.h:71-91）、#667 cachedOutputPath、8.0 WP-E operatorImplStamp（.h:81-91）、#1186 resumeOf 按值返回（.h:135-138） |
| 剩余疑点 | S-9 setAttempt 静默失败：`run->setAttempt(attempt()+1)` 在 attempt 已达 1e6 时静默不增（P3 不可达）；S-10 toJson 无写侧 cap（同疑点 3） |

### 10. WorkflowCheckpointManager（workflow_checkpoint.h 76 + .cpp 492 行）
| 维度 | 结论 |
|---|---|
| 权威数据源 | `<dir>/checkpoint_<runId>.json`（Engine-2 唯一持久权威）；`history/` 归档 keep=50（.cpp:217-240） |
| 主要调用者 | workflow_run_coordinator.cpp:440（persistRun）、:922（resume load）、:821（recoverAtStartup） |
| 错误模型 | save：写/flush/fault/rename 任一失败 → 删 tmp 返回空，旧文件不动（.cpp:79-127）；load：开文件/16MiB cap/JSON parse/version/runId/definition/stepPlans 全 fail-closed（.cpp:130-173 + workflow_run.cpp:635-656）；corrupt 恢复时 warn+skip（.cpp:439-444） |
| 资源上界 | 读侧 16MiB（.cpp:139）；election 读 cap+1（.cpp:280-283）；归档 keep=50；**写侧无 cap**；checkpoint 目录条目数无 prune（仅 Completed 归档路径 prune） |
| 测试覆盖 | test_workflow_recovery.cpp 10 case（原子 save/load、恢复、tmp 无残留、Running→Pending、corrupt/hollow skip、unsafe runId、oversized 拒、lock、活主跳过）；test_workflow_durability_13.cpp 9 case（lineage envelope、corrupt envelope 拒、*_resume 不并组、ghost 选举、finalized ghost inert、legacy 文件名规则、runId 语法 fuzz 335-399、恢复幂等、corrupt provenance 无染） |
| 历史修复痕迹 | #727（活主所有权 .cpp:452-471）、#1186（orphan 替换 .cpp:395-402；恢复前 sweep .cpp:412-426）、Track 13 D5（declared-lineage 选举 .cpp:256-317）、exact prefix/suffix strip（.cpp:343-350 注释） |
| 剩余疑点 | S-11 **写侧无 16MiB cap**（.cpp:74-88，对照 pipeline_run_coordinator.cpp:362-370 有）→ 大定义/大 resultPayload 可产出自身 loader 拒收的 checkpoint，恢复 warn 后跳过 = run 静默不可恢复（P1/P2）。S-12 loadCheckpoint 是 size-then-readAll TOCTOU（.cpp:139-149），electionGroupFor 已改 cap+1 且注释谎称 "same idiom as loadCheckpoint"（.cpp:278-280）（P2）。S-13 orphan sweep 在 per-run lock 检查**之前**无条件删 `checkpoint_*.json.tmp.*`（.cpp:414-425）→ 双进程并发时删掉活进程在写的 tmp，其后者的 rename 必败（fail-closed 但保存丢失）（P2）。S-14 `*.orphaned` 第三模式过宽（.cpp:421，P3） |

### 11. WorkflowRunLock（81+215 行）
| 维度 | 结论 |
|---|---|
| 权威数据源 | flock(2)（Unix）/QLockFile（Win）on `checkpoint_<runId>.lock`；liveness 只由锁原语决定（.h:8-22） |
| 主要调用者 | workflow_run_coordinator（submit :507-519、resume :889-910、finalize 释放 :802）、recoverInterruptedRuns（workflow_checkpoint.cpp:457-471）、probeOwner（--list-runs） |
| 错误模型 | 三态 TryResult，Acquired/HeldByLiveOwner/Error；metadata 仅诊断（.cpp:113-118） |
| 资源上界 | lock 文件从不 unlink（.h:21-22，防 unlink-while-held 竞态）→ 每个执行过的 run 永久留 ~40B 文件 |
| 测试覆盖 | test_workflow_recovery.cpp:307/347/394（互斥、活主跳过、lock 不混入 listing）；durability fuzz 含 lock 文件名形态 |
| 剩余疑点 | S-15 lock 文件无界累积（P3，已文档化设计代价）。**注意：此锁仅 Engine-2 使用，D17 完全没有对应物（见方向 2 结论 2）** |

### 12. WorkflowRunCoordinator（248+1512 行，Engine-2 生产接线）
| 维度 | 结论 |
|---|---|
| 权威数据源 | 内存 `m_runsByPipeline` / `m_pipelineByRunId` / `m_locksByRunId` / `m_resuming`（.h:209-214）；磁盘 checkpoint 为持久权威；线程模型 = mutex（非 affinity），锁序 flock→m_mutex→TaskCenter（.h:200-206） |
| 主要调用者 | TaskCenter::taskUpdated → onTaskUpdated（DirectConnection .cpp:583-587）；MCP run_workflow/resume_workflow；recoverAtStartup |
| 被调用者 | TaskCenter::submitPipeline/cancelPipeline/getPipelineInfo/getTaskInfo；ArtifactGC::sweepRun；WorkflowCheckpointManager |
| 错误模型 | persist best-effort（写败不停管线，.cpp:435-440）；resume 各失败路径 outError；通知 queue-under-lock/drain-outside-lock（#860，.cpp:309-399）；last-writer-wins persist seq + IO mutex（.cpp:401-451）；resume 内联 reconcile（.cpp:942-954）；completed-output 三重身份门（implStamp→file→stat→digest，.cpp:967-1069，全 fail-closed re-execute） |
| 资源上界 | digest 预算 256MiB/输出（env 可调，.cpp:40-51）；m_runsByPipeline 无界（保留全部历史 .cpp:803-806 注释） |
| 测试覆盖 | test_workflow_run_coordinator.cpp 11 case（tracked 完成并 persist、startup 恢复+resume、声明序无关边保留 #727、port-aware 解析、diamond/scrambled、多 completed 父+disconnected、lock 拒 resume、内联 reconcile、mixed 父替换、#750 out-of-band 替换重执行、#931/#944 延迟 IO 不阻塞他 run）；test_workflow_resume_provenance.cpp 2 case（跨边界 lineage、crash-after-step-1） |
| 历史修复痕迹 | #697/#668（.h:7-8）、#727（RC1/RC2/RC4 .cpp:1073-1136）、#720（swap 原子性 .cpp:680-684）、#860（事件生命周期 .h:153-169 + .cpp:756-760）、#931/#944（IO 出锁）、#950?/#1097（reverse-map 释放 .cpp:803-806）、#1078/#1078a/b（wedge 三处）、#750/#731、8.0 WP-E、#1178（ReplaceFileW .cpp:158-168）、#1186（ghost 不回写 .cpp:1287-1291）、#1228（pipelineIdForRun 兜底 .cpp:1413-1422） |
| 剩余疑点 | S-16 resume swap 后，swap 前被 TaskCenter 线程捕获的 ghost fold persist 可能在 `QFile::remove(ghost checkpoint)`（.cpp:1299）之后落盘 → 复活一个 Canceled（terminal、inert）ghost checkpoint 文件（persistRun 读的是 ghost 对象当前状态 .cpp:440），恢复/选举不受影响但文件泄漏（P2/P3，[LIKELY]）。S-17 cancelRun 的 `transitionTo(Cancelling)` 在锁外调（.cpp:1342，run 自身 mutex 兜底，可接受）；cancelPipeline 返回 false 且 plans 非全 terminal 时不 finalize（.cpp:1350-1394 只处理全 terminal 分支）——依赖“false ⇔ 全部已 terminal”的 TaskCenter 契约（P3，[UNKNOWN] 契约在 TaskCenter 侧）。S-18 单例与 TaskCenter 单例的静态析构顺序 + 永不 disconnect（.cpp:583-587；QObject 自动断连兜底，P3） |

### 13. PipelineRunCoordinator（219+1321 行，D17）
| 维度 | 结论 |
|---|---|
| 权威数据源 | `RunState`（def/statuses/remainingParents/cancelRequested/attempt）——只在 affinity 线程变更（.h:198-214, .cpp:460-487）；磁盘 `checkpoint_<uuid>.json`（run 目录内） |
| 主要调用者 | designer dock / production bind（makeRegistryNodeExecutor）、测试（makeSyntheticNodeExecutor）；signal：nodeStatusChanged/nodeFinished/pipelineCompleted/checkpointPersisted |
| 错误模型 | 全 marshal（invokeOnCoordinatorThread .cpp:309-342，marshal 失败丢调用不 inline #1186/D7）；requestCancel 两阶段（.cpp:657-693，#1158）；resume 校验全在 commit 前（#1078c .cpp:1144-1156）、未知 state key fail-closed（.cpp:68-76, 1185-1189）、cancel 落在验证中 → 拒 resume（.cpp:1267-1275）；persist 写侧 cap + 原子替换 + fault 注入（.cpp:352-402）；provenance 写败不败 run（.cpp:971-1000） |
| 资源上界 | pool ≤ 8（.cpp:58, 521-527）；checkpoint 读写 16MiB（.cpp:364, 1093-1097）；identity 模式 Auto= >2MiB 全文件（.cpp:184-196）；**无跨进程 run lock；无节点数上限** |
| 测试覆盖 | test_workflow_checkpoint_cache.cpp 36 case：含 #1158 cancel-race-resume（368）、#1152 cancel 到 executor（458）、diamond 单次执行（517）、cyclic/double-start 拒（568）、跨线程一致性（595）、foreign kind/version/oversized/unknown-state/cyclic-embedded 全拒（805-913）、legacy 1.0 保守重算（915）、篡改/外部/移动 artifact 不服务（954/996/2217）、executor 外部 artifact 失败（1040）、参数失效精确下游（1072）、Running 重算（1113）、cancelled→resume 全成功（1142）、subflow e2e（1241/1284）、provenance 4 case（1329-1532）、双 fuzz（1587/1641）、mid-file tamper 全文件方案（1808）、unknown tag 拒（1893）、异线程 identity 切换（1922）、活跃 run 拒 startRun（1951）、hash 中 cancel（1985）、异线程析构（2043）、并发读者不撕裂（2076）、publish 边界 crash（2130）、provenance publish 失败（2163）、corrupt provenance 无染（2189）、idle cancel 不毒化（2245）、resume 验证中 cancel（2269）。test_d17_workflow_pipeline_e2e.cpp 4 case（mini e2e、50% crash-resume、100 节点 scale、11 模板全绿） |
| 历史修复痕迹 | #1006（无 executor 拒绝 .cpp:812-819）、#1032、#1152、#1158（三处注释 .cpp:624-630, 660-693, 1080-1084, 1263-1275）、#1078c、#1077、#1186/D7、C-F6 GUI 泵（.cpp:158-170）、msbuild C2373 注释（.cpp:1299-1301）、#1009 fcntl build-unblock（.cpp:38-46） |
| 剩余疑点 | S-18（P1）无跨进程所有权锁：两进程同时 resume 同一 checkpoint 均成功，非缓存后缀双重执行（详见方向 2）。S-19（P1）whole-file hash 的 processEvents 泵（.cpp:163-176）期间可送达外来析构 marshal → 析构体在泵内跑完后，析构线程继续销毁 `m_state`，而 affinity 线程仍在 onNodeFinished 内使用它（.cpp:843 起 snapshot 引用 / .cpp:877 cancelFlag 指针）→ UAF 窗口；test 2043 的泵发生在测试线程、affinity 线程空闲于事件循环，未覆盖此窗口。S-20（P2）同一泵窗口内的重入 marshal（requestCancel hop、他节点 onNodeFinished→persist）依赖“活跃 run 拒 startRun/resume”这一道闸保证 statuses 引用不失效——任何未来在活跃期 insert statuses 的代码会把 snapshot 引用变成悬垂（QHash insert 可 rehash），脆弱设计点（.cpp:843 + 163-176）。S-21（P2）每节点完成全量重写 checkpoint：O(V²) 总写盘 + 每次全 JSON 序列化（.cpp:928, 1017-1057） |

### 14. ArtifactGC（73+270 行）
| 维度 | 结论 |
|---|---|
| 权威数据源 | 无状态；保护集来自注入的 ProtectedArtifactProvider（执行缓存 .cpp:29-49） |
| 主要调用者 | workflow_run_coordinator.cpp:444-445（Completed finalize sweep，retainFinalOutputs=true） |
| 错误模型 | 只扫 Completed run（.cpp:110-112）；删除走 rename→.gctrash→delete，失败回滚或入 report.errors（.cpp:61-91）；workspace 边界 = retained finals 的 canonical 目录（.cpp:143-163），界外一律不删（.cpp:209-211） |
| 资源上界 | 无（输入为 run plans） |
| 测试覆盖 | test_workflow_artifact_gc.cpp 7 case（sidecar、非 Completed 不扫、cacheHit 保留、#726 活声明保护/释放、界外拒、未完成步骤保留、retainFinalOutputs=false 全清 #697） |
| 历史修复痕迹 | #697（workspace 根 + retain=false 语义反转修复 .cpp:137-142）、#726（cache 生命周期 .cpp:168-186）、#1178（MoveFileExW 替换 .cpp:66-74） |
| 剩余疑点 | S-22 retainFinalOutputs=false 时“每个 Completed 输出目录都成根”（.cpp:137-142 注释自认 opt-in 全清）——若步骤输出落在共享目录（如 /tmp），同 run 其他中间件在该目录内可删；不越 run 提名集，风险受控（P3）。S-23 isPathUnderAnyRoot 大小写敏感（.cpp:93-101）与 D17 的 CaseInsensitive（pipeline_run_coordinator.cpp:240-248）策略不一（canonical 路径同为盘上大小写，实际影响低，P3 一致性） |

### 15. WorkflowProvenance（92+275 行）
| 维度 | 结论 |
|---|---|
| 权威数据源 | 每终态 attempt 一份 `provenance_<runId>.json`（attempt>1 进 `attempt-<N>/`，pipeline_run_coordinator.cpp:976-996） |
| 主要调用者 | PipelineRunCoordinator::finalizeIfDone（写）；测试读回验证 |
| 错误模型 | envelope 闸（kind/version 拒，.cpp:121-132）；条目 ≤ 2²⁰（.cpp:15, 138-140）；id 空缺/重复/kind 未知/边悬挂全拒（.cpp:142-169）；写败不败 run |
| 资源上界 | 读侧 1M 条目 cap；无文档字节 cap（调用方未见于 src/workflow，[UNKNOWN] 外部读取方） |
| 测试覆盖 | checkpoint_cache:1329/1392/1443/1483（roundtrip 稳定、reusedFrom 边、失败 run 也有记录、envelope/悬挂/重复/kind 全拒）、fuzz 1641、corrupt 无染 2189 |
| 剩余疑点 | fromRunState 每节点扫全部边（.cpp:244-270）O(V·E)（P2 归方向 5） |

### 16. placeholder_grammar / workflow_session / workflow_runtime（275+254+837 行）
| 维度 | 结论 |
|---|---|
| 权威数据源 | WorkflowSession：m_paramsByStep/m_artifacts/m_completed（mutex）；WorkflowRuntime：m_defs/m_sessions/m_cancelFlags/m_activeTaskIds（mutex） |
| 主要调用者 | runtime/session 是 TaskPanel 交互路径；resume 共享同一 resolver（workflow_run_coordinator.cpp:1087-1094）；TaskCenter 参数替换同语法 |
| 错误模型 | runtime 抛 std::runtime_error；plane 路径 `PlaneTaskFailure` 防同步重放（.cpp:41-48, 256-281）；**fallback 靠异常消息字符串匹配 "ExecutionPlane submit failed"**（.cpp:272，P3 脆弱）；commit/rollback marshal 10s 超时 + late-delivery kill（.cpp:560-587, 735-751）；await 硬编码 30min（.cpp:427）；gate 未知 require fail-closed |
| 资源上界 | session 数无上限；markStepComplete 有 completed≤steps 帽（workflow_session.cpp:193-194）；cancel watcher 20ms 轮询线程（.cpp:441-457） |
| 测试覆盖 | test_workflow_runtime.cpp 24 case（含 #503 并发回归 630）；test_workflow_cancel.cpp 1 case（requestCancel 中止 operator step）；test_pipeline_runner.cpp 11 case（含 #313 workspace containment 477、退化/空管线不 stall 437） |
| 历史修复痕迹 | #702/#703（DataManager 亲和 marshal .cpp:555-559, 719-723）、#727（端口解析单策略 .cpp:336-346, 694-702）、#698（sidecar .cpp:610-634）、#503 |
| 剩余疑点 | S-24 session 路径 runStepSync 记录的 artifact path 无任何 confinement（对照 ir2 executor 全链 confinement）——交互路径产物落盘位置由 operator 决定（P3，威胁模型不同）。S-25 resolvePlaceholderPort 第 3 级 fallback 做大小写不敏感端口扫描（placeholder_grammar.cpp:200-214）——非 ASCII/异常大小写端口可能解析到错误端口数据？不会：仍要求精确 equal 忽略大小写的同名字符串值，风险极低（P3） |

### 17. workflow_limits（15 行）
- 全部内容 = `kMaxCheckpointDocumentBytes = 16MiB`（.h:13）。
- 执行点：engine-2 读（workflow_checkpoint.cpp:139, 280）、D17 写+读（pipeline_run_coordinator.cpp:364, 1093）。
- 未执行点：engine-2 写（S-11）；节点/边/subflow 展开体积/目录条目数均无该文件之外的上限常量。

---

## 五个重点深挖方向：结论

### 方向 1 — Workflow IR2 lowering

**结论：解析/校验分层正确、生产路径全部 fail-closed，但“fromJson 宽、validateSemantics 严”的两段式是结构性风险；unknown field 采取“忽略”策略；无任何规模上限。**

- unknown field：**忽略，不是 reject**。fromJson 只查必填字段（workflow_ir_v2.cpp:184-279），未知顶层/节点/端口键静默丢弃；测试甚至 pinned 该行为（test_workflow_ir_v2.cpp:425 "Unknown top-level and metadata keys survive the round-trip"）。版本闸（.cpp:186-190）保证“未来版本文档”被显式拒绝而非静默降级 —— 设计自洽，但“2.2 文档谎报 2.1”时新字段静默丢失（P3，固有限制）。
- 形状/类型：port 八字段逐一类型检查（.cpp:47-84），错型即拒；数值无范围检查（bandCount 可为负/巨大，.cpp:75-78；resolution 无 NaN 界，.cpp:34-45）——仅影响 contract/cost 展示层（P3）。
- 重复 nodeId/edgeId、幽灵端点、入度>1：不在 fromJson，在 validateSemantics（.cpp:343-344, 355-356, 359-383）。生产两处 fromJson 调用点（composer .cpp:120→147、resume .cpp:1124→1136）+ startRun 直接 validate（.cpp:594）都补齐了；V1 迁移终点也 validate（.cpp:510）。**任何新增 fromJson 调用方漏配 validate 都会把重复/悬挂边放进运行**（P2 流程性）。
- 自环/循环依赖：validateSemantics **不查自环**（A.out→A.in 合法通过），由 analyzeDag 兜底拒（startRun .cpp:614-616、resume .cpp:1139-1142、composer .cpp:306-310）；resume 对被篡改成环的 checkpoint 有专项拒收 + 注释（.cpp:1128-1142）。净效果 fail-closed。
- 深嵌套 JSON：MCP 入口 stackLimit=64 + Json::Exception 捕获（workflow_run_coordinator.cpp:461-474，#1154）；checkpoint 读入路径用 jsoncpp 默认 stackLimit（~1000）（workflow_checkpoint.cpp:152-156）——不对称但仍有界（P3）。
- 规模上界：**无节点/边/端口数上限**；subflow 深度 8 是唯一结构性上限。lineage 签名 O(V²E)、DAG O(V+E)、per-node 边扫描 O(E)（见方向 5）。

### 方向 2 — Checkpoint（engine-2 + D17）

**结论：磁盘原子性双端达标； Engine-2 恢复/选举/身份验证是教科书级 fail-closed；两个真实缺口：D17 无跨进程 resume 所有权锁（双重执行），Engine-2 写侧无体积帽（毒 checkpoint）。**

- 写盘原子性：Engine-2 唯一 per-save tmp（pid+counter）+ fsync(file) + `std::filesystem::rename`（POSIX rename/MoveFileEx REPLACE_EXISTING）+ dir fsync（workflow_checkpoint.cpp:69-127）；D17 同构且多写侧 cap + MOVEFILE_WRITE_THROUGH + fault 注入点（pipeline_run_coordinator.cpp:344-402）。写败均删 tmp 不晋级。[FACT]
- attempt 单调性：Engine-2 每次 resume `setAttempt(attempt()+1)`（workflow_run_coordinator.cpp:1322-1326，先增后 persist 顺序正确）；读侧拒 <1/>1e6（workflow_run.cpp:687-701）；setter 对越界静默忽略（.cpp:360-367）。D17 resume `attempt = recorded+1`（pipeline_run_coordinator.cpp:1283-1286，#1186 修复 attempt-less 默认）。[FACT]
- cache invalidation：D17 = lineage 签名重算 + recorded-tag 指纹（fl/full，未知 tag 拒 .cpp:213-227）+ size+mtime+canonical containment 四重（.cpp:1204-1256）；Engine-2 = operatorImplStamp 门（.cpp:981-1006）+ 非空真文件（.cpp:1008-1041）+ stat + digest 必须复验（.cpp:1049-1066）+ 池重水化再验（.cpp:124-170）。篡改/移动/截断路径全部重算。[FACT]
- corrupt/partial 读取：全部 fail-closed 拒收（state 词表 .cpp:68-76、未知节点 state .cpp:1185-1189、vocabulary/重复 plan id workflow_run.cpp:746-776、半提交由原子写保证不存在）。恢复 pass 对 corrupt 文件 warn+skip 不阻塞他 run（workflow_checkpoint.cpp:439-444）。[FACT]
- quarantine/orphan sweep：选举按 declared lineage（resumeOf）+ legacy 文件名后缀迁移路径，非恢复候选 terminal 文件 standalone 不被并组（.cpp:250-317）；输家 rename→`.orphaned`（替换式，#1186）；恢复启动时 sweep tmp+orphaned（.cpp:412-426）。**疑点 S-13：sweep 先于 per-run lock 检查，双进程并发可删活进程在写的 tmp（后果=该次保存失败，fail-closed）。[LIKELY]**
- Engine2 resume ghost：swap 时 ghost 置 Canceled（通知但不回写 #1186）+ 删 ghost checkpoint + 释放 ghost lock（workflow_run_coordinator.cpp:1271-1300）；crash 留下的 ghost 由选举按 lineage 归组、original 存活（electable 过滤 .cpp:373-382）；finalized ghost inert（isRecoveryCandidateState .cpp:250-254）。恢复出过期 artifact 被 #750 三重身份门拦截。**疑点 S-16：swap 前已捕获的 ghost fold persist 晚到可复活 terminal ghost 文件（无害但泄漏）。[LIKELY]**
- 跨 schema 版本：Engine-2 闭集 {1,2}（workflow_run.cpp:643-656），v1→attempt=1 无 resumeOf（.cpp:681-686）；D17 闭集 {1.0,1.1}，legacy 保守重算（测试 915）。[FACT]
- 同一 checkpoint 重复恢复：Engine-2 = flock + `m_resuming` + already-tracked 拒（.cpp:854-938）+ 活主拒（.cpp:883-910）；**D17 = 进程内 “already active” 拒（.cpp:591, 1077）+ checkpointPath 覆写同一文件，但无任何跨进程锁** —— 两个进程对同一文件 resumeFromCheckpoint 都会成功（验证、CacheHit 判定、dispatch 各自独立进行）。checkpoint 写有唯一 tmp 不互相撕裂，但双方各自 dispatch 非缓存后缀 = 同一工作流剩余节点双重执行；后写者 checkpoint 覆盖先写者。现有测试只覆盖同 coordinator 的 double-start（568）与异线程活跃拒（1951）。**判定：P1 完整性缺口。[FACT：代码中无 lock；后果推断 LIKELY]**

### 方向 3 — 协调器状态同步

**结论：两个协调器线程模型不同但各自自洽；marshal/锁序/通知外排纪律都有注释和回归测试钉住；剩余风险集中在 D17 hash 泵重入窗口与析构的交互。**

- 状态如何同步：PipelineRunCoordinator 单 affinity 线程（所有公有入口 BlockingQueuedConnection marshal，.cpp:309-342；marshal 失败丢弃不 inline，.cpp:326-331）；WorkflowRunCoordinator 纯 mutex + 全局锁序（.h:200-206）+ fold 整体临界区保证 swap 原子性（#720 .cpp:680-686）。两者通过 TaskCenter 松耦合（Engine-2 订阅 taskUpdated；D17 独立事件循环），不直接互同步。[FACT]
- cancel 生命周期 / 晚到回调：D17 两阶段（原子 phase-1 先行，marshal phase-2 落 affinity；晚到 worker 经 QPointer + shuttingDown + removePostedEvents 三重丢弃，.cpp:796, 823-826, 836-841, 503-511）；#1158 三处修复（不清 stale flag、idle cancel 自清、resume 前 set flag 则拒收）+ 专项测试（368/2245/2269）。Engine-2：cancelRun 先 persist Cancelling 再 cancelPipeline，false 分支内联 finalize 防 wedge（#1078b .cpp:1349-1394）；finalize 后晚到 fold 安全（terminal 闸 .cpp:745）。[FACT]
- marshal/线程亲和：D17 状态字段只允许 affinity 线程触碰，读侧全部 marshal（isRunning/getAllStatuses/provenancePath 等 .cpp:539-572）；Engine-2 的 WorkflowRun 内部 mutex 覆盖 mutation/serialization（workflow_run.h:103-114），两个 escape hatch（definition()/findStepPlan()）明示单写者假设。[FACT]
- timeout：Engine-2 协调器对 TaskCenter 调用无超时（同步 getPipelineInfo/getTaskInfo，[UNKNOWN] TaskCenter 侧是否可能阻塞）；workflow_runtime 有 30min await + 10s commit/rollback marshal 超时 + late-delivery kill flag（.cpp:427, 576, 748）。[FACT]
- TaskCenter 回调在对象销毁后到达：Engine-2 单例与 TaskCenter 单例互相 QObject 生命周期管理，DirectConnection 从 worker 线程直达 onTaskUpdated，回调内先查 m_runsByPipeline（.cpp:660-663）；静态析构顺序噪声（S-18，P3）。D17 不接 TaskCenter 信号。[FACT]
- 进程重启 reverse-map：`m_runsByPipeline/m_pipelineByRunId` 纯内存，重启后恢复靠 checkpoint + recoverAtStartup 重建（.cpp:810-838）；#1097 让 terminal run 释放 reverse map 以便同进程 resume（.cpp:803-806），#1228 补了 mission reconcile 的兜底查找（.cpp:1413-1422）。[FACT]
- Qt signal/slot 生命周期：Engine-2 connect 一次（m_connected）永不断（.cpp:580-588）；runStateChanged 只在 drainRunNotifications 锁外发（.cpp:393-398），重入安全靠 swap-under-lock（.cpp:383-386）。[FACT]
- **P1 疑点 S-19**：D17 whole-file hash 在 affinity=GUI 线程时每 1MiB 泵一次 processEvents（.cpp:158-176，C-F6）。窗口内可送达外来析构 marshal：析构体（.cpp:504-511）在泵内执行完毕后，析构线程继续销毁 `m_state`（unique_ptr 成员析构），而 affinity 线程仍停在 onNodeFinished 栈帧内，恢复执行时写 `snapshot`（statuses 内引用，.cpp:843）并读 `m_state->cancelRequested`（.cpp:877 循环条件）→ UAF。现有 foreign-destruction 测试（2043）里泵发生在测试主线程、affinity 线程空闲在事件循环，未命中此窗口。[LIKELY：时序成立，未实际复现]

### 方向 4 — 路径 confinement

**结论：没有共享 helper；三处独立实现策略不完全一致；ir2 执行器链最完整（nodeId 语法→声明输出→发布产物 canonical 三层）；Engine-2 resume 对产物路径无 containment（以 digest 为完整性锚，威胁模型使然）；GC 删除边界以 canonical workspace 根 + run 提名集双重约束。**

- helper 位置：`isSafeNodeId`/`canonicalRunDirectory`/`resolveArtifactPath`/`isInsideRunRoot`（ir2_registry_node_executor.cpp:61-99）；`isContainedInDirectory`（pipeline_run_coordinator.cpp:233-249，canonical + Win/macOS CaseInsensitive，#1186）；`isPathUnderAnyRoot`（artifact_gc.cpp:93-101，canonical + 大小写敏感）。`src/workflow/` 外唯一相关：model_catalog（src/operators/framework/model_catalog.*）。**无 workflow_path_util。**
- 写入时 canonicalize：ir2 执行器对**发布产物** canonical 化后查 containment（.cpp:253-257），对**声明输出**只 cleanPath（.cpp:172-174）——runRoot 内 symlink 指向外部时声明检查放过、发布检查拦下（报错时机靠后，净 fail-closed）。D17 onNodeFinished 对 executor 返回产物 canonical 查 containment（.cpp:857-867 `ir2.artifact_outside_run:`）。
- symlink：执行器发布检查 canonical + isFile（symlink→外部文件：canonical 外部→拒；symlink→目录：isFile 假→拒）（.cpp:249-264）。D17 resume `isContainedInDirectory` 用 canonicalFilePath，穿透 symlink（.cpp:233-249）。
- `..`/绝对路径逃逸：resolveArtifactPath 相对路径锚定 runRoot、绝对路径 cleanPath 后必须仍在 root 内（.cpp:79-85 + 185-193 拒收信息）；nodeId 语法层禁止 `/ \ ..`（.cpp:61-67，#1032）。
- Windows UNC/盘符：cleanPath 保留 UNC 形态，前缀比较天然拒绝跨盘符/跨 UNC sibling（.cpp:87-99 注释明示）；MoveFileExW/`std::filesystem::rename` 语义已统一（workflow_run_coordinator.cpp:158-168 #1178）。
- macOS 大小写不敏感：仅 D17 isContainedInDirectory 处理（.cpp:240-248）；executor/GC 比较的两侧均取自 canonicalFilePath（盘上大小写），实际风险低但不一致（P3）。
- artifact_gc 删除边界：只删 Completed run 提名的、canonical 后落在 retained-final workspace 根内的、非 leaf/非 artifact、非 cacheHit、非 cache 活声明的文件；删除经 `.gctrash` 两阶段；失败进 report.errors 不静默（.cpp:105-217, 245-268）。**篡改 checkpoint 无法提名 workspace 外路径**（根集合来自现存 finals 的 canonical 目录，.cpp:143-163）。

### 方向 5 — 资源上界

**结论：磁盘文档上限一处（16MiB）且 Engine-2 写侧缺失；并发/深度/条目数上限各有一处但分散；图算法与 checkpoint 写放大是无护栏的 O(N²) 级；取消响应延迟设计良好。**

| 上界 | 值与位置 | 强制者 |
|---|---|---|
| checkpoint 文档 | 16MiB（workflow_limits.h:13） | D17 写+读（pipeline_run_coordinator.cpp:364, 1093）；Engine-2 读+election（workflow_checkpoint.cpp:139, 280）；**Engine-2 写不设防（S-11）** |
| provenance 条目 | 2²⁰（workflow_provenance.cpp:15） | fromJson（:138-140） |
| 线程池 | 8（pipeline_run_coordinator.cpp:58） | clamp（:521-527） |
| subflow 深度 | 8（workflow_composer.h kMaxSubflowDepth） | expandSubflowsImpl（:269-272） |
| 归档 keep | 50（workflow_checkpoint.cpp:237-238） | archiveCompletedRun |
| attempt | 1e6（workflow_run.h:51） | setAttempt/fromJson（workflow_run.cpp:363, 695） |
| digest 预算 | 256MiB（workflow_run_coordinator.cpp:40-51） | computeCompletionIdentity |
| JSON 深度 | MCP 64（workflow_run_coordinator.cpp:462）；jsoncpp 默认 ~1000（checkpoint 读） | #1154 |
| **节点/边数** | **无上限** | — |
| **checkpoint 目录条目** | **无 prune**（仅 history/ 有 keep=50；`checkpoint_*.json` 无界累积，每 run 一个 + 每 run lock 文件永久留存 workflow_run_lock.h:21-22） | — |

O(N²) 级算法（大 workflow 风险，均无告警/上限）：
- lineage 签名：定点扫描 × 每节点 parentsOf O(E)（plan_optimizer.cpp:89-123, 22-30）→ 最坏 O(V²E)；startRun 与每次 resume 各跑一遍。
- frontier 排空：定点循环 × 全 remainingParents 扫描 × 每候选全边表扫描（pipeline_run_coordinator.cpp:718-772）→ O(V²E) 最坏；每节点完成再 O(E)（.cpp:917-924）。
- checkpoint 写放大：每节点完成全量序列化重写（.cpp:928→1017-1057）→ 总写盘 O(V²·doc)。
- provenance 构建 O(V·E)（workflow_provenance.cpp:244-270）；GC 依赖集 O(V·E)（artifact_gc.cpp:118-131）。
- 100 节点 scale 测试存在（test_d17_workflow_pipeline_e2e.cpp:270-310），更大的规模行为完全未验证。

日志/JSON 无界增长：m_pendingRunNotifications 每公共路径即排空（workflow_run_coordinator.cpp:753, 760）；trace 为有界环（.cpp:329-334 注释）；checkpoint 内 resultPayload/rawParams/resolvedParams 三份参数随定义无界（间接受 16MiB 读帽约束——但见 S-11 写侧无帽）。

取消响应延迟：D17 —— phase-1 原子先于 marshal（.cpp:657-663）；hash 每 ≤1MiB chunk 轮询（.cpp:129, 165-176）；executor 侧 #1152 注入 cancel flag；专项测试 1985（hash 中 cancel 即返回）与 458（flag 到 executor 及时性）。Engine-2 —— cancelPipeline 即时 + runtime 侧 20ms watcher（workflow_runtime.cpp:441-457）。[FACT：设计达标]

---

## Top 疑点清单（按优先级）

### P1-1 D17 checkpoint 双进程重复恢复 → 剩余节点双重执行
- **现象**：`PipelineRunCoordinator` 的 startRun/resumeFromCheckpoint 全链无 flock/所有权原语（pipeline_run_coordinator.cpp 全文无 WorkflowRunLock 引用；run 目录内只有 checkpoint/provenance/attempt-N）。两个进程（或一台机器上两个实例）对同一 `checkpoint_<runId>.json` 同时 resume 均返回 true，各自把非 CacheHit 后缀 dispatch 一遍，checkpoint 文件互相覆盖。
- **怀疑根因**：D17 设计于 designer 本地单用户场景（runId=UUID、run 目录私有），跨进程所有权当时不存在；Engine-2 后来补了 #727 锁体系，D17 未跟进。头注释只讨论了“两进程写同一 tmp 不撕裂”（.cpp:344-348），证明覆盖写竞态被考虑过而双重执行没有。
- **复现思路**：在 tests/test_workflow_checkpoint_cache.cpp 框架内：`chain(4)` 造 checkpoint（node1 Succeeded）；两个 coordinator 实例 + 计数 executor（模拟两进程可用两个线程 + 各自独立 RunState，或起两个测试进程）；同时 resumeFromCheckpoint 同一路径；断言 `executed == 2`（当前代码必然双跑）。修复方向：run 目录内 `checkpoint_<runId>.lock` 复用 WorkflowRunLock，resume commit 前持锁。

### P1-2 D17 whole-file hash 泵窗口 × 外来析构 = UAF
- **现象**：affinity=GUI 线程时，onNodeFinished 内的 sha256full hash 每 chunk `processEvents`（pipeline_run_coordinator.cpp:163-176）；期间送达外来 `delete coordinator` 的 marshal（.cpp:504-511）后，killer 线程继续析构 `m_state`，而 affinity 线程的 onNodeFinished 栈帧（snapshot 引用 .cpp:843、`&m_state->cancelRequested` .cpp:877）恢复执行 → use-after-free。
- **怀疑根因**：C-F6 修复（GUI 泵）引入重入点，但析构协议假设“析构体跑完 affinity 线程就离开危险区”，未考虑析构体恰在**另一线程的泵内**执行的情况。
- **复现思路**：改造 test_workflow_checkpoint_cache.cpp:2043 的 foreign-destruction 测试：executor 产 8MiB artifact（强制 Auto→full hash），killer 线程在 hash 开始后立即 delete；主线程不泵（让析构 marshal 落在 hash 的泵里）需要 affinity 线程就是 GUI 线程 + killer 不等待——用 ASAN/TSAN 跑现有套件最省力。

### P1/P2-3 Engine-2 checkpoint 写侧无 16MiB cap → 自产毒 checkpoint
- **现象**：`saveCheckpoint` 不查序列化体积（workflow_checkpoint.cpp:74-88），`loadCheckpoint` 拒 >16MiB（:139-147）；大定义/大 resultPayload 的 run 写出后，recoverAtStartup 与 resumeRun 一律拒载（warn "skipping corrupt checkpoint … size cap"）——run 静默变成不可恢复，且管线继续跑完（persist best-effort，workflow_run_coordinator.cpp:435-440）无任何用户可见失败。
- **怀疑根因**：cap 引入时只改了读侧与 D17 写侧（D17 注释 "Writer honours the reader's cap" pipeline_run_coordinator.cpp:362-363），Engine-2 写侧漏改。
- **复现思路**：test_workflow_recovery.cpp 框架：构造 WorkflowRun，往某 step 的 resultPayload 塞 >16MiB 字符串，saveCheckpoint 成功 → loadCheckpoint 返回 null → recoverInterruptedRuns 不报 recovered。修复：saveCheckpoint 写前比对 `kMaxCheckpointDocumentBytes`，超限 fail（与 D17 对齐）。

### P2-4 Engine-2 orphan sweep 可删活进程在写的 tmp
- **现象**：recoverInterruptedRuns 开头无条件删 `checkpoint_*.json.tmp.*`（workflow_checkpoint.cpp:414-425），此发生在 per-run lock 检查（:457-471）**之前**且无任何全局协调锁；双进程同时启动时，B 可删掉 A 正在写的 tmp → A 的 rename 失败 → A 本次保存丢失（fail-closed，但恢复窗口拉长）。
- **怀疑根因**：#1186 注释假设 "Recovery runs at startup, before any new saves"（:412-413）——单进程假设在多进程 CLI/GUI 并存场景不成立。
- **复现思路**：进程 A 用 `setCheckpointIoDelayForTests` 拖住 persistRun（同 API 改造成跨进程不便，可先写单进程双线程 + 人为 tmp 命中测试：目录预置 `.tmp.<otherPid>.*` 文件 + 活锁持有者探测），断言 tmp 被删。修复：sweep 跳过 mtime < N 秒的 tmp，或 sweep 前尝试对应 run 的 lock。

### P2-5 空 stepId 定义 → 自产不可恢复 checkpoint
- **现象**：`workflowDefinitionFromJson` 对空 id 的 step 跳过重复检查（workflow_definition.cpp:121-132）；两个空 id step 通过 `createFromDefinition` 变成两个 `stepId:""` 的 plan；首个 checkpoint 落盘后，`WorkflowRun::fromJson` 以 "duplicate stepPlans id ''" 拒收（workflow_run.cpp:769-772）→ 永久不可恢复。单空 id 则与任何后续空 id plan 冲突、`setStepStatus("")` 打到第一个匹配。
- **怀疑根因**：定义解析器与 checkpoint 校验器的词表不对齐（解析器容忍、校验器拒绝）。
- **复现思路**：test_workflow_run_coordinator.cpp fixture：definition 两个 `id=""` step + 双 executor → startTrackedPipeline 成功 → 直接 `WorkflowCheckpointManager().loadCheckpoint(该 checkpoint)` 返回 null。修复：解析器拒空 id（与 `isValidRunId` 同风格的 fail-closed）。

### P2-6 resume swap 后 racing fold persist 复活 ghost checkpoint 文件
- **现象/根因/复现**：见 S-16（workflow_run_coordinator.cpp:1271-1300 vs :686-760 与 :417-451）。复现：resumeRun 的 swap 临界区与一个人为延迟的 onTaskUpdated（对 ghost pipelineId 的晚到 taskUpdated）并发；swap 完成后让 fold 的 persistRun 落盘；断言 `checkpoint_<ghostRunId>.json` 重新存在（内容为 Canceled，inert 但泄漏）。修复方向：swap 时给 ghost 打 “已废弃” 标记（对象内 atomic），persistRun 对已废弃 runId 跳过写。

### P2-7 大图 O(V²E)/O(V²) 热点无护栏
- **现象/根因**：见方向 5 列表（plan_optimizer.cpp:89-123、pipeline_run_coordinator.cpp:718-772/928、workflow_provenance.cpp:244-270、artifact_gc.cpp:118-131）。100 节点内被 scale 测试覆盖，千节点级 startRun/resume 延迟与写放大无度量。
- **复现思路**：把 test_d17_workflow_pipeline_e2e.cpp:270 的 `layerCake100` 参数化为 1000 节点深链，量 startRun 耗时与 run 目录写盘字节数（现测试已有 elapsed/RSS 采集样板）。

### P2-8 `qJsonObjectToJsonCpp` 静默吞参（fail-open 到“无参执行”）
- **现象**：node.parameters 经 QJsonDocument::Compact 往返 jsoncpp 解析失败时返回空对象（ir2_registry_node_executor.cpp:28-40），operator 拿到 `{}` 继续执行（可能写出默认输出）——参数丢失被静默成“合法空参运行”。
- **触发条件**：QJsonValue 含 NaN/Inf（程序内构造）等不可序列化值；正常 JSON 来源不可达，故 P2 下限。
- **复现思路**：test_ir2_port_param_mapping.cpp 框架构造 parameters 含 `QJsonValue(double quietNaN)` 的 NodeFact，经 makeRegistryNodeExecutor 执行，断言当前静默成功。修复：解析失败 → `ir2.operator_failed: parameters unparsable` 拒绝。

### P3（择要）
- P3-9 test_workflow_run_coordinator.cpp:230-232 断言 `<runId>.json` 不存在——文件名漏了 `checkpoint_` 前缀，断言恒真（vacuous），未验证“完成后 checkpoint 已归档”。
- P3-10 三处 containment helper 大小写策略不一（executor/GC 敏感、D17 Win/macOS 不敏感）——建议抽公共 `workflow/path_util`。
- P3-11 lock 文件与 `*.orphaned` 无界累积（workflow_run_lock.h:21-22、workflow_checkpoint.cpp:421）。
- P3-12 `WorkflowRun::setAttempt/setResumeOf` 静默忽略非法值（workflow_run.cpp:360-382）；建议 [[nodiscard]] bool。
- P3-13 workflow_runtime 同步 fallback 靠异常消息字符串匹配（workflow_runtime.cpp:269-279）。
- P3-14 engine-2 checkpoint 读入路径 jsoncpp 默认 stackLimit（~1000）与 MCP 入口 64 不一致（workflow_checkpoint.cpp:152）。
- P3-15 cancelRun 依赖 “cancelPipeline()==false ⇔ 全部任务已 terminal” 的 TaskCenter 侧契约，workflow 侧无法自证（workflow_run_coordinator.cpp:1349-1394）。

---

## 测试覆盖矩阵（14 个指定文件 → 组件）

| 测试文件 | 覆盖组件 | 关键 case |
|---|---|---|
| test_workflow_ir_v2.cpp（449 行，16 case） | IR2 | golden round-trip、fail-closed 拒收族、单源不变量、V1 迁移、2.1 additive、unknown key 存活 |
| test_workflow_engine_v2.cpp（279，7） | WorkflowRun/definition | 状态机、创建、JSON roundtrip、runId、invalid payload、#697 |
| test_d17_workflow_pipeline_e2e.cpp（363，4） | D17 全链 | mini e2e、50% crash-resume、100 节点 scale（1.5GiB RSS）、11 模板全绿 |
| test_workflow_durability_13.cpp（465，9） | Engine-2 durability | lineage envelope、corrupt envelope、*_resume 不并组、ghost 选举/inert/legacy、runId fuzz、恢复幂等、corrupt provenance |
| test_workflow_checkpoint_cache.cpp（2332，36） | D17 + provenance + affinity | 见组件 13 行（fuzz×2、#1158/#1152、身份四重、publish 边界、cancel-resume 验证） |
| test_workflow_run_coordinator.cpp（1084，11） | Engine-2 协调器 | 见组件 12 行（#727 系、#931/#944、#750） |
| test_workflow_cancel.cpp（123，1） | runtime cancel | requestCancel 中止 operator step |
| test_workflow_recovery.cpp（419，10） | Engine-2 checkpoint/lock | 原子 save/load、恢复、corrupt skip、unsafe runId、oversized、#727 lock 三连 |
| test_workflow_resume_provenance.cpp（354，2） | Engine-2 resume lineage | 跨边界 lineage+resolved inputs、crash-after-step-1 |
| test_workflow_composition.cpp（472，12） | composer + plan signature | 见组件 4 行 |
| test_workflow_artifact_gc.cpp（295，7） | GC | 见组件 14 行 |
| test_pipeline_runner.cpp（521，11） | CLI runner/TaskCenter | schema 校验、$step.output、#313 containment、退化管线不 stall、python 混合 |
| test_context_checkpoint.cpp（306，6） | ⚠️ 非 workflow 组件 | agent context session 存取/压缩 —— 与 src/workflow 无 include 关系，硬ening 时应排除 |
| test_workflow_runtime.cpp（671，24） | runtime/session/gate/builtin | gates、builtin 模板、placeholders、#503 并发 |

相关但不在指定清单：tests/test_ir2_port_param_mapping.cpp（mapping+executor confinement+#1152）、tests/test_fault_injection.cpp:186（archiveCompletedRun keep=50）。

## 覆盖缺口（与疑点一一对应）
- D17 跨进程/双实例 resume 竞态（P1-1）——零覆盖。
- 外来析构 × hash 泵窗口（P1-2）——现测试 2043 未命中窗口。
- Engine-2 写侧体积（P1/P2-3）——只有读侧 oversized 测试（test_workflow_recovery.cpp:249、checkpoint_cache:841）。
- orphan sweep × 活跃保存（P2-4）、空 stepId（P2-5）、ghost persist 复活（P2-6）——零覆盖。
- >100 节点规模（P2-7）——上限即测试上限。
- mapping mixed 分支（S-3）——零覆盖。
