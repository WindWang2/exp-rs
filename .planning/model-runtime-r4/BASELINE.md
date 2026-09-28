# BASELINE — Track 15: Model Runtime & Python Integration Hardening R4

实测时间：2026-09-27（本机 Linux / gcc-15 栈）。所有行号基于本 worktree 基线提交。

## 1. 基线 SHA 与 worktree

| 项 | 实测值 |
|---|---|
| `origin/master` | `15e5c66b543ef3874cb929f17529ef456bd6c059`（= PR #1333 合并点，与提示词写作时一致，**未前进**） |
| 本地 `master` | `eac910dff9`（落后 origin/master 159 提交，0 领先；master 根目录只读） |
| 本轨道分支/工作区 | `hardening/r4-model-runtime` @ `/home/kevin/projects/rs-studio/exp-rs-model-runtime-r4`（独立 worktree，基于 origin/master） |
| 构建 | `build-gcc15`，`-DCMAKE_C_COMPILER=gcc-15 -DCMAKE_CXX_COMPILER=g++-15 -DCMAKE_CXX_COMPILER_LAUNCHER= -DENABLE_TESTS=ON`，Unix Makefiles，`-j2`（显式清空 launcher 规避 raise-compiler-stack.sh 假绿陷阱；GCC16 本机有 Debug ICE，必须 g++-15） |
| 资源红线 | `CMAKE_BUILD_PARALLEL_LEVEL=2`、`ctest -j1`；RSS>70% 降 -j1（记录于账本） |

## 2. 文件清点实测（与提示词锚定表的漂移）

| 锚点 | 写作值 | 实测值 | 漂移说明 |
|---|---|---|---|
| `src/runtime` 文件数 | 36 | **37**（含 src/runtime/CMakeLists.txt） | chunk 19 / exec 2 / gpu 2 / observability 10 / worker 3 / CMakeLists 1 |
| `src/python` 文件数 | 23 | **33** | isolated/ 16、根 6、scripts/ 1、sdk/ 10（含 CMakeLists 与 sdk/exprs/*.py） |
| 独立 SessionPool 文件 | 不存在 | **不存在（复核成立）** | `rg -ln "SessionPool|session_pool" src/runtime` → 仅 gpu_plane.{h,cpp} |
| 池 class | gpu_plane.h | `ModelSessionPool` @ gpu_plane.h:96（acquireSession :104 / releaseSession :108 / evictStale :117；Impl @ gpu_plane.cpp:18） | 一致 |
| trace_id.h | src/runtime/trace_id.h | **实际在 `src/runtime/observability/trace_id.h`**（TraceIdGenerator :27、kTraceSchema :45、TraceContext :48） | 提示词路径漂移 |
| worker_protocol.h | 锚点 | 帧构造/解析 inline 函数族（makeErrorFrame :145、parseFrame :162、frameErrorCode :211、frameErrorMeansCancelled :226、frameOutputIdentity :237）；消费方=processing LocalWorkerHost 栈（local_worker_host.cpp / local_worker_pool.cpp / worker_process_io.h / cli/sicnu_worker_main.cpp），**python_worker_provider 不消费它**（消费 provider_wire.h） | 详见 DECISIONS.md D-2 |
| worker_lease.h | 锚点 | `WorkerLeaseTracker` :51（onLiveness :62 / onJobStart :66 / onJobOutcome :69 / verdict :72 / mayTakeover :77 / reset :80）；消费方=local_worker_pool | 一致 |

## 3. ADR 0130 契约三态对账（真值来源 docs/adr/0130-model-runtime-platform-4.md）

| # | 契约条款 | 状态 | 依据 |
|---|---|---|---|
| D1 | manifest 4.0 内容身份（digest 进身份、id/manifest_version/license/source） | **部分覆盖**（词汇/形状有测；身份顺序与 digest 不在纯校验面） | validateManifestJson 不查 digest、不传 manifest 路径（mc.cpp:2750-2766）；身份检查排在 aux 内容检查之后（:531-563 vs :486-525） |
| D2 | 运行时契约（classifyInferenceError 六类、cancel/warmup/health） | 已覆盖 | test_model_failure_matrix.cpp（ADR 明示） |
| D3 | 确定性设备选择 | 已覆盖（operators 层）；src/runtime 池的 device pin 语义**无测** | gpu_plane.cpp:75 的 pin 检查无专测 |
| D4 | 原子分块推理（tmp+rename、OOM batch→tile、batch=1 终局） | 部分覆盖 | test_chunk_contract_11/test_chunk_resume_11 覆盖原子性家族；但 tile_run_contract 分区算术 0/负/INT_MAX 未加固未测（tile_run_contract.h:55-63） |
| D5 | OOM 阶梯 | **仅模拟覆盖**（gpu_plane 4 用例用 fake backend 模拟 allocateVram 失败；真实配额路径无测） | tests/test_gpu_plane.cpp（133 行） |
| D6 | 检测解码与 provenance | 已覆盖 | test_model_runtime_8/9 |
| D9 | 有界 session 池（LRU、PoolStats） | operators 层已测；src/runtime ModelSessionPool 的 Busy 公平界/双持/释放语义**部分无测** | test_gpu_plane.cpp 仅 4 用例；test_model_runtime_stress.cpp 测的是 operators 层池 |

## 4. 在途 PR 盘点与文件重叠（2026-09-27 实测 7 个 open PR）

| PR | 分支 | 文件数 | 与本轨道白名单重叠 |
|---|---|---|---|
| #1334 fix/review-p1-security | 同名 | 30 | `src/operators/framework/model_catalog.{h,cpp}`（仅 defaultModelsDirectory 去 CWD）、`src/operators/runtime/python_worker_provider.{h,cpp}`（解释器白名单+worker_script containment，**acquire 时**生效）、`tests/CMakeLists.txt`、`tests/test_provider_python.cpp`。**P1-8/9/10 明示不处理** |
| #1335 fix/review-p0-build-restore | 同名 | 19 | `tests/CMakeLists.txt`、`mission-runtime-gate/tests/test_mission_runtime_scale.cpp` |
| #1336 closure-ui-runtime-r4 | 同名 | 13 | `tests/test_temporal_scene_model.cpp` |
| #1337 closure-workflow-contracts-r4 | 同名 | 25 | mission-runtime-gate 两个测试（不重叠白名单） |
| #1338 closure-io-processing-r4 | 同名 | 29 | processing 算法文件（不重叠） |
| #1339 r4-i18n-help | 同名 | 38 | `tests/CMakeLists.txt` |
| #1340 r4-operator-oracles | 同名 | 24 | `tests/CMakeLists.txt` |

**重叠策略**：`tests/CMakeLists.txt` 是 5 个在途 PR 的公共冲突点——本轨道的新测试 target 追加在文件尾部独立 hunk，rebase 时按 hunk union。model_catalog/python_worker_provider 本轨道只做 P1-8 非安全遗留（错误信息与一致性），不碰 #1334 的白名单/containment 语义；若 #1334 在本轨道收尾前合并，rebase 落位；若未合并，PR 正文声明"基于 pre-#1334 master，#1334 合并后需一次语义 union"（见 DECISIONS.md D-3）。

**#1335 的 142 个既有失败分类中落在本轨道 ctest 过滤面的**：`test_provider_http`（5，退出阶段段错误类）、`chunk_contract_11`（科学/逻辑断言类）、`execution_plane_8`（同上）、`mission_runtime_*`（不在过滤面）。基线红绿分布以本机实测为准（§7）。

## 5. 评审材料通读记录

- 本地 `review/findings/{operators.md,pi.md}` 存在（deep review R2 材料）；`PROJECT_REVIEW_DOSSIER_5.0.md`、`AUDIT_DOSSIER_ISSUES_747_760.md`、`PR_TRIAGE_REPORT_2026-09-16.md`、`docs/PARALLEL_TRACKS_10.md` **均不存在**（以 PR 描述为准）。
- P1-8 原文：深度评审 commit `1750fa93`（对 `d6b7a058`），本地不可达；以 #1334 正文"P1-8/9/10 不属于安全问题，留待后续处理"与本轨道提示词定义为准：**manifest 校验的错误信息与一致性**。
- #1336 正文声明其 PR 未跑 C++ 测试（Windows 环境），其"未解决项"不与本轨道重叠（簇 A/B/E）。

## 6. provider 可用性实测

- 可选部署 provider 编译门：`SICNU_ENABLE_TENSORRT`（operators/CMakeLists.txt:309，默认 OFF）、`SICNU_ENABLE_OPENVINO`（:323，默认 OFF）、`SICNU_WITH_ONNX_RUNTIME`（:337，条件编译）。**provider 实际代码位于 `src/operators/runtime/{tensorrt_provider,openvino_provider,onnxruntime_provider}.{h,cpp}`，不在 src/plugins（提示词猜测路径实测零命中）**——白名单边界修正记入 DECISIONS.md D-1。
- 本机 SDK 可用性：待 §7 基线构建后以 cmake 输出复核（TensorRT/OpenVINO SDK 未安装的预期结果=requested but not found）。

## 7. 基线红绿分布（本机实测）

构建完成后回填：`ctest -R "provider|model|runtime|session|interpreter" -j1` 全量结果、红测逐个分类（既有红 vs 本轨道引入）。

## 8. 本轨道边界声明（白名单）

允许触碰：`src/runtime/**`、`src/python/**`、model provider 相关（实测=`src/operators/runtime/*provider*` 守护面 + `src/operators/framework/model_catalog.{h,cpp}` 的 P1-8 校验链，见 DECISIONS.md D-1/D-2）、`tests/test_provider_*`、`tests/test_model*`、`tests/test_runtime*`、`tests/` 下本轨道新增测试与 `tests/CMakeLists.txt`（尾部追加）、`.planning/model-runtime-r4/**`。白名单外一律拒绝并记账。

## 9. 既有测试载体清单（过滤面内）

test_provider_python / test_provider_algorithm_adapter / test_provider_fallback / test_provider_http / test_preflight_provider / test_onnxruntime_provider / test_model_catalog_v2 / test_model_ensemble / test_model_failure_matrix / test_model_library_manifests / test_model_manifest7 / test_model_runtime{,_8,_9,_bench,_stress} / test_model_selector / test_model_tasks / test_gpu_plane / test_worker_lease_11 / test_worker_host / test_chunk_{graph,contract_11,resume_11,adoption_11} / test_execution_{governor_11,authority_11,scale_fault_11} / test_external_memory_10 / test_trace_contract / test_python_engine / test_python_plugin_host 等。

## 10. Round 2 基线（2026-09-28，PR #1353 合并后重入）
- origin/master 实测 `a726d17a6`（= PR #1354 ci-redzone 合并点）；Round 1 基线 15e5c66b5 以来
  master 前进 414 提交（#1334–#1354 整波 R4 全部并入）。
- Round 1 交付（PR #1353，65 用例）已在 master；分支 `hardening/r4-model-runtime-r2` 自
  a726d17a6 新开，worktree 复用 exp-rs-model-runtime-r4（构建目录 build-gcc15 延用，g++-15/
  Debug/Makefiles/LAUNCHER 空）。
- **master P0 断裂 #1（已修，提交 ae9fcf1616）**：tests/CMakeLists.txt:14728 孤立 `=======`
  冲突标记（R4 合并列车遗留），cmake configure 必败，全仓 blocked。全库 tracked 扫描无第二处。
- **master P0 断裂 #2（D-14 解锁）**：tests/support/qt_lifecycle.h:48（#1342 e4b3d245df）g++
  硬错误，打穿 teardown 族全部测试 TU + 本轨道白名单内 test_provider_http。
- 白名单域漂移：src/runtime+src/python 在 414 提交中仅 8 文件变动 = Round 1 自身合并内容
  （gpu_plane/chunk_graph/tile_run_contract/execution_governor/python_worker_process*）——域仍
  归本轨道，无外来语义冲突。
- 遗留项#4 实测升级：tile_spec.h 在 15e5c66b5..master 字节一致 → length_error→overflow_error
  翻转发生在更早的 #1056 提交 3070d3e1ad，test_chunk_contract_11 三用例为 master 长期既有红
  （"chunk" 不匹配过滤面正则所以 Round 1 未见于红榜）。
- 过滤面基线红绿分布：构建完成后回填（§10.1）。
