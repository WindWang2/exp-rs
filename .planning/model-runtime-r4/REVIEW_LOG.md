# REVIEW_LOG — Track 15 Model Runtime R4 独立对抗评审记录

评审者：context-sweeper 只读 agent（与实现分离），2026-09-27，对 `origin/master...HEAD` 全量 diff
+ 关键测试二进制复跑。结论：**BLOCK**（1×P0 + 3×P1），全部处置后 SHIP。

## P0 — publish fence 在构造 throw 路径泄漏（已修复）

**证据**：`model_publish.cpp` 在 insert 后置 `m_fenceHeld=true`，随后 4 个 throw 点
（window A/B 恢复 rename 失败、prov park 失败、main park 失败含 armed park fault）都在
ctor 体内——throwing ctor 不跑 dtor，`releasePublishFence()` 永不执行。
确定性复现：`tests/test_model_tasks.cpp:1126/1139`（12802 断言套件）——park fault 块之后
swap fault 块的 guard 构造抛 `AlreadyRunning`，`.tmp~` stage 残留、最后
`REQUIRE_NOTHROW( run() )` 失败。生产可达：4 个调用点（tile_inference_engine.cpp:2171/2217/3819、
model_ensemble.cpp:1629）都传 parkFaultPoint。

**修复**：RAII `PublishFenceSlot`（作用域退出即释放，含 throw 路径）；dtor 自身释放保持幂等。
提交 9585a895cc。

**复验**：`test_model_tasks` 12802/12802 全绿；`test_runtime_publish_orphan_r4` 87/4 全绿。

## P1 — 10 个新测试未注册进 tests/CMakeLists.txt（已修复）

**证据**：`git show HEAD:tests/CMakeLists.txt | grep -c r4` = 0——注册块只在工作树。
**修复**：提交 91bba57f35（append-only 块 + TEST_PREFIX，使 ctest 过滤面可逐一计数）。

## P1 — stopWorker() 切断 stderr 捕获连接且不重武装（已修复）

**证据**：`python_worker_process.cpp:129` blanket disconnect 把新增的
readyReadStandardError lambda 一并断开；`ensureSignalsConnected()`（#523 的同族修复）
只重连 finished/errorOccurred。直接复用实例（startWorker 内部对 isRunning 调 stopWorker）
后 capturedStderr() 永久为空。
**修复**：ensureSignalsConnected 补第 4 个连接（提交 9585a895cc）。

## P1 — verifyArtifactLocked 的 fail lambda 绕过路径前缀（已修复）

**证据**：`model_catalog.cpp:2009` 自有 fail lambda 无 sourceManifest 前缀——
artifact/digest 族发现（MissingArtifact/ChecksumMismatch/"artifact not found"）不带
manifest 路径，P1-8 四元组在校验面不一致。
**修复**：fail lambda 统一加前缀（提交 9585a895cc）。

## P2 — stopWorker() 尾部的 group sweep 是死代码（已修复）

**证据**：Qt 文档 `QProcess::processId()` 在进程结束后返回 0；clean waitForFinished 后
killProcessTree 必为 no-op，注释承诺的行为不成立。
**修复**：删除死调用，注释改为诚实说明（有效 sweep 只在 timeout 分支，tree.sh 用例覆盖）。

## P2 — governor 泄漏报告"隔离"是 last-writer-wins（文档化）

**证据**：ctor 清 static + dtor 写 static；重叠生命周期下（B 在 A 作用域内构造）A 的 dtor
会覆盖 B 的观察窗。测试是严格串行的，重叠情形未覆盖。
**处置**：注释改为精确声明"sequential case is the contract, the overlap case is
best-effort by design"（提交 9585a895cc）。不改语义（进程级单槽报告是既定设计）。

## P2 — kMaxSessionsPerDevice 是纯改名 + ladder-empty 旁路未文档化（文档化）

**证据**：常量提取零行为变化；`&& !ladder.empty()` 使零档请求绕过公平界并落入
CpuFallback——真实但未声明的交互。
**处置**：ctor 内注释声明该旁路（提交 9585a895cc）。

## P3 — 小项

- `QDir::cleanPath` fence key 无大小写折叠/符号链接解析：大小写敏感文件系统上两种拼写各得
  独立 fence。接受（类注释已说明是 canonical cleanPath；平台特定行为不在本轨道范围）。
- `setpgid` 返回值未检查：接受（POSIX 上 setpgid(0,0) 在同组内几乎不失败；失败后果是
  sweep no-op，非正确性问题）。
- manifest 测试的 expectQuadruple/anyIssueCarries 在**拼接后**的 readinessReason 上做子串
  匹配：四元组可能由不同 finding 满足。接受并记录——断言的是"合同面存在性"，逐 finding
  精确匹配需要把 readinessReason 拆分为结构化数组（API 变更，超出本轨道）。
- `tileSpecAt` 三次除法两次即可：O(1) 不变式不受影响，接受。
- 白名单记账不全：`.gitignore`(+4)/`.goal-loop-ledger.md`(+12) 在 D-9 之外。已在 D-9 补记。
- EVIDENCE §1 曾漏列 `test_chunk_contract_11`（master 既有红，`std::length_error` vs
  `std::overflow_error`，在本轨道过滤面外）。已在 EVIDENCE 更正。
- PROVIDER_OOM_MATRIX 的 ONNX 归因不精确：实为 `SICNU_WITH_ONNX_RUNTIME` option 默认 OFF
  （opt-in），非"无 SDK"。已更正。

## 复审确认（评审者验证干净的关键点）

- gpu_plane AcquiredReduced 判定在锁内；无生产调用方（仅测试），爆炸半径为零。
- chunk_graph null gate 与 ChunkPipeline::validateBuffer 同型同址（try 内、cancel 检查之后）。
- tile_run_contract typed throw 对所有既有调用方安全（chunked_run 预检、journal 校验索引）；
  零档分区从 SIGFLE 变为 typed 逃逸是改进。
- model_publish dtor fence 释放恰好一次、幂等、释放于 restore 之后。
- python stderr lambda 生命周期安全（context=this，QObject 析构自动断开）。
- **#1334 重叠为零**：gh api 实证本分支 model_catalog hunk 全在 parseManifest/
  ModelDetectionContract::validate/validateManifestJson，#1334 的 hunk 全在
  defaultModelsDirectory；python_worker_provider 未被触碰。
- src/agent/CMakeLists.txt 两行链接必要且语义中性（符号定义位置与 add_subdirectory 顺序实证）。
- 测试真值来源审计：抽 5 个边界用例均为真实边界（非模拟充数）；OOM 六档中唯一不可达档
  （硬件显存）诚实标注为模拟。18 个既有相关套件（model_manifest7/runtime_8/9/
  library_manifests/catalog_v2/tile_semantic_equivalence/gpu_plane/chunk_adoption/
  chunk_resume/governor/scale_fault/external_memory/ensemble_scene/ensemble_parallel/
  plugin_model_bridge/failure_matrix/runtime_stress/model_runtime）全部保持绿。

## 一处测试弱点（记录不修）

`test_runtime_python_channel_r4.cpp` 的 in-flight 恢复用例手工调用
`request.callback( error, true )` 验证"恢复请求以 typed error 收尾"——驱动的是测试自身
簿记而非 pool 的真实恢复路径（pool 恢复涉及时钟/重启时序，超出本边界用例的确定性预算）。
记录为已知限制。
