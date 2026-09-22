# 02 — History Dedup: agent/flash-workflow-integrity vs origin/master

Track: Workflow / Checkpoint / Resume / Cancel / Path Integrity hardening。
基线: origin/master = `a9dc33fa7329a0cf4b40fe838bb7c6177ad2ee01`。
旧线索分支: `agent/flash-workflow-integrity`（PR #1030–#1069 时代）。
方法: 逐线索 commit 读全量 diff → 追 master 侧演变（`git log <sha>..origin/master -- src/workflow/`）→ 对每个关键防护在 master 现行文件中核对 file:line → 对照 master 测试集评估杀伤力。所有行号基于 a9dc33fa。

## Executive Summary

1. 六个线索 commit 的核心防护 **全部已有 master 等价物或更强替代**，无一需要原样移植。
2. master 的路线已经升级：size+mtime 时间戳 → **sha256fl/sha256full 内容指纹**；stamp-authorship → **delete-stale-before-execute**；共享 path_containment.h → **两份本地实现**（语义有分叉）。
3. 真正剩下的候选（按价值排序）：IR2 执行器 `isSafeNodeId` 缺 `':'`（NTFS ADS）拒绝；恢复 sweep 缺 per-file 异常隔离（防御纵深）；IR2 pre-execute declared-output 逃逸（绝对路径 / `..` / output_path 绕过）**无测试**；两份 containment 实现语义分叉可整合；IR2 执行器对「非声明结果的 run 内陈旧文件被认领」无执行窗口校验。
4. 线索测试 commit 的 oracle 有两处已被 master **有意反转**：legacy 无戳 checkpoint 现在「全量重算」而非「containment 放行」；wrong-typed meta.ui 现在「静默跳过」而非「typed 拒绝」。移植旧测试会直接与现行设计冲突。
5. PR #1200/#1225/#1224/#1235 已覆盖：executor 协作取消、cancel-swallow 修复、attempt 编号、ghost/election 隔离、marshal-failure 语义、大小写不敏感 containment、Windows 原子 rename、fsync 真实化。

## 逐线索五分类表

| 线索 commit | 主题 | 分类 | master 吸收/替代 |
|---|---|---|---|
| `3ff2b27b0c` path_containment.h | run-dir containment 共享 helper | **已被后来架构取代** | `f4758f2069`(#1107) 改为文件内本地 helper；无共享头 |
| `5b6091599a` IR2 authorship | artifact 作者校验 | **已被后来架构取代**（残留 2 点，见缺口清单 #1/#3） | `f4758f2069`(#1107) delete-stale 方案 |
| `00cea41dec` checkpoint read bounds | 读取上界 + per-file 隔离恢复 | **大部分已完全进入 master**（残留 per-file catch，见缺口清单 #2） | cap: `297ab55198`（改名 `kMaxCheckpointDocumentBytes`，cap+1 读）；per-file try/catch 未吸收 |
| `519f855b41` meta.ui type guard | 反序列化类型防护 | **已完全进入 master**（语义改为静默跳过） | `d2a723ee23`（#1038 同批 typed-JSON 修复），现行 `isNumeric`/`isBool` 守卫 |
| `3ef5c39a7d` marshal + resume stamps | 线程亲和 marshal；resume 诚实性 | **已被后来架构取代**（更强） | marshal: `f4758f2069` → Track 13 D7 `invokeOnCoordinatorThread` → `9a23736a48`(#1225)；stamps: Track 13 内容指纹（`7c76ba56cb`/`9842e51690`/`82308915df`） |
| `0ec1bfa66d` 测试 commit | 4 个测试文件 831 行 | **只是历史测试线索**（大部分已有同等/更强 master 测试；2 个 oracle 已被有意反转；3 个杀伤点仍缺，见缺口清单 #4） | 见下方逐用例映射 |

## 四主题现状核对

### path_containment

- **master 没有 `path_containment.h`**。containing 逻辑分成两份本地实现：
  - IR2 执行器（`src/workflow/ir2_registry_node_executor.cpp:69-99`）：`canonicalRunDirectory` / `resolveArtifactPath` / `isInsideRunRoot`（纯词法前缀比较，全平台大小写敏感）。用途：pre-execute 声明输出收口（:161-195）、post-execute 结果收口 + canonical symlink 复查（:234-264）。
  - 协调器（`src/workflow/pipeline_run_coordinator.cpp:233-249`）：`isContainedInDirectory`（双侧 canonical，**Windows/macOS 大小写不敏感**，#1186 修复）。用途：发布门 onNodeFinished（:861）与 resume 验证（:1228）。
- 分叉点：IR2 侧不做大小写不敏感比较（runRoot 经 canonicalFilePath 已是磁盘大小写，实际风险低），coordinator 侧做。共享头 idea（线索 `3ff2b27b0c` 的动机「one implementation」）未落地，属可选整合候选。

### workflow_limits

- `src/workflow/workflow_limits.h:13`：`kMaxCheckpointDocumentBytes = 16 MiB`（原线索名 `kMaxCheckpointReadBytes` 已弃用）。由 `297ab55198` 引入。
- 强制点（全部强于线索版「size-then-readAll」——改为 **cap+1 读**，封死「检查后增长」TOCTOU）：
  - `workflow_checkpoint.cpp:139`（size 检查）+ `:280-283`（election 读，cap+1）
  - `pipeline_run_coordinator.cpp:1093-1097`（resume 读，cap+1）+ `:364-370`（**写入侧**同 cap——本构建写不回读的文档直接拒绝发布）
- 其他上界：`kMaxPoolWorkers = 8`（`pipeline_run_coordinator.cpp:58,524`）；`kMaxRunAttempt = 1000000`（`workflow_run.h:51`，`workflow_run.cpp:363,695`）；整文件 hash 分块 1 MiB（`pipeline_run_coordinator.cpp:129`）。

### checkpoint integrity（读取边界 / 隔离恢复 / 原子写）

- 读取边界：见上，cap+1 已是更强形态。
- 隔离恢复：`recoverInterruptedRuns`（`workflow_checkpoint.cpp:408-490`）per-file 跳过 corrupt（:436-445，靠 loadCheckpoint 返回 nullptr + typed err）。**没有**线索版的 try/catch(Json::Exception/std::exception)。master 的赌注是反序列化器源头全防护（`workflow_run.cpp:212-287,635-788` 每个cast 前 isMember+isType；`workflow_definition.cpp:174-196`）。
- 恢复 sweep 已远超线索时代：tmp/orphaned 清扫（:412-426）、Phase J election + resume-ghost 隔离（`electCheckpoints` :321-405，`*.orphaned` rename 隔离，#1225 改 `renameReplaceQuiet`）、跨进程 run-lock（#727，:453-471）。
- 原子写：协调器 `atomicWriteJson`（`pipeline_run_coordinator.cpp:352-402`）tmp(pid+counter 唯一) → flush → fsync → rename → dir fsync，Windows `MoveFileExW(REPLACE_EXISTING|WRITE_THROUGH)`；Engine-2.0 侧 checkpoint 用 `geospatial/util/atomic_fs`（#1224/#1225）。

### pipeline state sync（marshal / 线程亲和）

- 每个公共入口都 marshal：`invokeOnCoordinatorThread`（`pipeline_run_coordinator.cpp:309-341`）统一模式；`setExecutor`(:514)/`setMaxParallelism`(:521)/`setArtifactIdentityMode`(:529)/`checkpointPath`(:539)/`provenancePath`(:544)/`isRunning`(:551)/`hasCompleted`(:559)/`getAllStatuses`(:564)/`startRun`(:576)/`requestCancel`(:657)/`resumeFromCheckpoint`(:1060)。
- 超出线索版的三点：marshal 失败（对象垂死）**丢弃调用而非回退内联**（#1186/D7，:328-331）；cancel 两阶段（phase-1 原子先置、phase-2 marshal，:657-673）；#1158 取消标志生命周期（start 不清、resume 见 flag 即拒绝、terminal 才清）。
- affinity 线程为 GUI 线程时整文件 hash 每块 pump 事件（#1186/C-F6，:158-176）。

## 线索测试 commit（0ec1bfa66d）逐用例杀伤力映射

| 线索用例 | 想杀的 bug | master 等价测试 | 结论 |
|---|---|---|---|
| test_ir2: outside-run claim（存在即成功） | `QFileInfo::exists` 冒充作者 | `test_ir2_port_param_mapping.cpp:375` case 2（`escapes the run directory`） | 已覆盖（更强：coordinator 发布门也测，`test_workflow_checkpoint_cache.cpp:1040`） |
| test_ir2: 声明 output 绝对路径逃逸、operator 未执行 | pre-execute 收口缺失 | **无直接等价**（master IR2 测试只测结果路径逃逸与 unsafe nodeId） | **缺口（清单 #4a）** |
| test_ir2: `..` 相对逃逸 | 词法逃逸 | **无直接等价**（仅 `../escape` 作 nodeId，:396） | **缺口（清单 #4a）** |
| test_ir2: output_path 绕过 output | 双 key 收口缺失 | **无直接等价** | **缺口（清单 #4a）** |
| test_ir2: 陈旧 leftover ghost → `ir2.artifact_stale` | 存在非作者 | `test_ir2_port_param_mapping.cpp:375` case 3（master 语义改为预删除 → `missing artifact`） | 已覆盖，oracle 错误码不同 |
| test_ir2: 诚实 rewriter 成功 | 防误杀 | case 5（relative output 成功） | 已覆盖 |
| test_ir2: traversal nodeId 拒绝 | id 插值逃逸 | `test_ir2_port_param_mapping.cpp:375` case 1（`unsafe nodeId`） | 已覆盖（`:` 变体除外，清单 #1） |
| integrity: 16 MiB 读取上界 | 无界 readAll | `test_workflow_recovery.cpp:249`（`size cap`） | 已覆盖 |
| integrity: wrong-typed meta.ui **typed 失败** | Json::LogicError 逃逸 | `test_workflow_execution_plane.cpp:410`（断言 `parsed == true`，静默跳过） | 已覆盖，**oracle 有意反转** |
| integrity: sweep 中 corrupt/wrong-typed/oversize 逐文件隔离 | 单坏文件杀全 sweep | `test_workflow_recovery.cpp:182`（corrupt/hollow 跳过 + healthy 恢复）；oversize/wrong-typed 不在 sweep 内联变体 | 大体覆盖（组合变体缺，低价值） |
| state_sync: 外线程 hammer 状态查询一致性 | QHash 撕裂读 | `test_workflow_checkpoint_cache.cpp:595` + `:2087`（并发读者不撕裂） | 已覆盖（更强） |
| state_sync: 外线程 requestCancel 应用并串行化 | cancel 竞态 | `:2000`（hash 中取消）、`:2300-2304`（resume/cancel 竞态）、`test_workflow_cancel.cpp:70` | 已覆盖（更强） |
| state_sync: 外线程 startRun 返回后状态可见 | 启动竞态 | `:1947`（外线程 startRun 被拒而非竞态——master 语义为 active 时拒绝） | 已覆盖 |
| cache: 篡改 artifact → 重算 | 假 CacheHit | `test_workflow_checkpoint_cache.cpp:954`（同长同 mtime 只剩指纹能抓——更强） | 已覆盖（更强） |
| cache: 记录路径在 run 外 → 不信任 | 越界 resume | `:996`（锻造合法指纹仍被 containment 拒） | 已覆盖（更强） |
| cache: **legacy 无戳 checkpoint 仍按 containment 缓存命中** | 兼容性 | `:915` 断言**全量重算、零 CacheHit** | **oracle 有意反转**（master 选择 fail-safe 冷启动） |
| cache: 超限 resume fail-closed | 无界读 | `:841` | 已覆盖 |

## 当前 master 仍缺失的防护清单（后续实现候选，按价值排序）

1. **`isSafeNodeId` 缺 `:`（NTFS ADS）拒绝** — 线索 `5b6091599a` 有，master 丢失。
   现状：`ir2_registry_node_executor.cpp:61-67` 只拒 `/`、`\`、`..`。`"a:b"` 形 nodeId 生成 `a:b.out.tif`，在 NTFS 上是文件 `a` 的备用数据流；词法收口与 canonical 检查都不拦。Windows-only、低概率但一行修复。
2. **恢复 sweep 缺 per-file 异常隔离（防御纵深）** — 线索 `00cea41dec` 有，master 未吸收。
   现状：`workflow_checkpoint.cpp:436-445` 无 try/catch，完全依赖反序列化器源头全防护（现已逐一核对 `workflow_run.cpp`、`workflow_definition.cpp` 确为全防护）。任何一个未来新增的无守卫 cast / jsoncpp 怪癖都会再次让**单个坏 checkpoint 杀死整个启动恢复**。补 `catch(Json::Exception|std::exception)` 每文件隔离即可，改动极小。
3. **IR2 执行器对「非声明结果的 run 内文件」无作者窗口校验** — 线索版有 execution-window mtime 残防，master 无。
   现状：master 只预删除**声明**的 staleTargets（`ir2_registry_node_executor.cpp:218-220`）；operator 结果指向 run 内其他陈旧文件（非声明、未删除、上轮遗留）时，post-execute 只有 containment + isFile（:240-264），会放行。线索版的 `mtime >= startedAtMs` 校验（3ef5c39a7d 所在分支的实现）可补此缝。敌意/出 bug 的 operator 才可触发，优先级中低。
4. **测试缺口（借鉴线索 0ec1bfa66d，勿搬 oracle 错误码）**：
   - (a) IR2 pre-execute 声明输出逃逸三连：绝对路径 `output` 逃逸、相对 `..` 逃逸、合法 `output` + 逃逸 `output_path` 绕过 —— 当前 master 测试只覆盖结果路径逃逸与 nodeId。落点：`tests/test_ir2_port_param_mapping.cpp` 的 confine 用例。
   - (b) IR2 侧 containment 与 coordinator 侧 containment 的语义分叉（大小写敏感度）——建议整合为单一 helper（即把线索 `3ff2b27b0c` 的共享头 idea 落地，统一 Win/macOS 大小写语义）后补跨平台用例。
5. **（一致性小项）`atomicWriteJson` 未走 `atomic_fs`**：`pipeline_run_coordinator.cpp:385-398` 手写 `MoveFileExW`/`rename`，而 #1224 已为 Engine-2.0 checkpoint 与 artifact_gc 集中到 `geospatial/util/atomic_fs`。行为正确但双轨，可顺手统一。

## master 侧 #1200 / #1225 / #1224 / #1235 已覆盖类别摘要

- **`82308915df` (#1200, Post-13.0 deep-review 批量)** — workflow 部分：
  - #1152 executor 协作取消（NodeExecutor 增 `cancelRequested` 参数；IR2 执行器把它接进 `RSOperatorContext`，`ir2_registry_node_executor.cpp:222-229`）。
  - #1158 外线程 requestCancel 被 start/resume 吞掉（标志生命周期重设计；resume 见 flag 拒绝提交，`pipeline_run_coordinator.cpp:1080-1084,1263-1275`）。
  - #1154/#1155 三个未迁移 JSON 读点的深度炸弹（含 `WorkflowRunCoordinator::startTrackedPipelineJson`）。
  - 同批还有 mission/SAR/spectral/插件锁等非本 track 项。
- **`9a23736a48` (#1225, P3 durability batch, #1186 部分)** — attempt 无值 v1.1 resume 默认 attempt=2（防 provenance 覆写，`pipeline_run_coordinator.cpp:1283-1286`）；election 隔离改 `renameReplaceQuiet` 且可替换既有 `.orphaned`（`workflow_checkpoint.cpp:390-403`）；恢复 sweep 清扫 `.orphaned`（:412-426）；Engine-2.0 resume 不再重持久化 Canceled ghost；`resumeOf()` 按值返回（`workflow_run.cpp:369`）；整文件 hash GUI pump（C-F6）；marshal 失败丢弃而非外线程内联（D7）；containment Win/macOS 大小写不敏感。
- **`e06282a69e` (#1224)** — publish rename 集中到 `geospatial/util/atomic_fs`：`workflow_run_coordinator.cpp`、`artifact_gc.cpp` 等 14 文件；Windows 语义（替换既有目标）统一。注意 pipeline 协调器自带 `atomicWriteJson` 未迁移（见候选 #5）。
- **`14bef28949` (#1235)** — `fsync_compat.h`（真实 FlushFileBuffers 替换 Windows fsync no-op，chunk/tile_checkpoint/journal）；`workflow_run_coordinator.cpp:1408-1425` 终态 run 经 `m_runsByPipeline` 历史反查（mission reconcile 不再丢成功判定）。
- 另两个直接相关但不在指定清单里：`297ab55198`（D17 checkpoint envelope/identity 的地基：kind/version gate、指纹、cap）、`01d904297c`（resume 先验证后变更 + 内嵌 workflow 语义/环校验，`pipeline_run_coordinator.cpp:1128-1156`；local-map 原子提交 :1158-1259）。

## 不可现代复现说明

线索分支基于旧基线（PR #1030-#1069 时代）：`path_containment.h`、`workflow_limits.h` 旧名 `kMaxCheckpointReadBytes`、`marshalBlocking`、`recordArtifactStamp`、`artifactSizeBytes/artifactMtimeMs` 快照字段等符号在现代 master 均不存在或已改名/升级；`ir2.artifact_outside_run:`/`ir2.artifact_stale:` 错误前缀在 IR2 执行器中已改为 `ir2.operator_failed:` 体系（仅 coordinator 发布门保留 `ir2.artifact_outside_run`）。任何「移植」判断都必须以现代符号为准，旧 diff 只作意图参考。
