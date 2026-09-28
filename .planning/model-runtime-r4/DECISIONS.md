# DECISIONS — Track 15 Model Runtime R4

## D-1 白名单边界修正：provider 文件实测位置
提示词把 model provider 写在 `src/plugins/`；实测 TensorRT/OpenVINO/ONNX provider 全部位于
`src/operators/runtime/{tensorrt_provider,openvino_provider,onnxruntime_provider}.{h,cpp}`，
编译门为 `SICNU_ENABLE_TENSORRT`/`SICNU_ENABLE_OPENVINO`/`SICNU_WITH_ONNX_RUNTIME`（src/operators/CMakeLists.txt:305-340），
`src/plugins/` 内 rg 零命中。WP-E 的"provider 守护矩阵"按实测落位到 src/operators/runtime provider 文件
（只守护既有开关语义，不新增 provider 代码）。src/operators/runtime/python_worker_provider.{h,cpp} 与
src/operators/framework/model_catalog.{h,cpp} 同理是 P1-8/WP-F 的实测接缝（提示词点名的
worker_protocol.h 实测不做 manifest 校验）。此为"白名单内实测空、接缝在白名单字面外"的测量修正，
非越界扩权：全部改动仍限定在 model-runtime/python 通道域。记账可审计。

## D-2 WP-A 接缝实测修正
提示词称 manifest 校验链在 `worker_protocol.h`+`trace_id.h`；实测 worker_protocol.h 是 LocalWorkerHost
帧协议（无 manifest 语义），manifest 校验真身在 `model_catalog.cpp` 的
`parseManifest`/`validateManifestJson`/`verifyArtifactLocked`（RUNTIME_FILE_MAP.md §七）。
WP-A 主体落在 model_catalog.cpp 校验链；worker_protocol.h/trace_id.h 侧仅做错误契约一致性
（frameErrorCode 分类对齐既有错误码族，不新开码表）。

## D-3 #1334 重叠策略
#1334 未合并（实测 2026-09-27）。本轨道基于 pre-#1334 master（15e5c66b5）：
1. WP-A 只做 #1334 未触碰的错误信息/一致性/顺序（其 model_catalog hunk 仅 defaultModelsDirectory）；
2. interpreter/worker_script 的 acquire 期校验**不重复实现**——本轨道仅把"catalog 纯校验面与执行面
   结论不一致"作为 P1-8 一致性缺陷记录并加 validate 层镜像断言（若 #1334 合并造成语义重叠，
   rebase 时以其语义为准做 union，不回退其白名单）；
3. tests/CMakeLists.txt 新 target 追加在尾部，5 个在途 PR 同文件冲突按 hunk union。

## D-4 子代理使用
Phase 0 用了 2 个只读 context-sweeper（chunk/exec 扫读、P1-8 校验链扫读）；Phase 5 预留 1 个只读
对抗评审。全程 ≤3，无递归派生。subagent token 消耗计入账本。

## D-5 既有红测处置
基线 ctest 中"本来就红"的测试（#1335 分类 + 本机实测）不修（非本轨道范围，除非落在白名单内且
与本轨道改动同因），逐个记账分类；本轨道 Oracle 是"零新增失败"，不是"全仓绿"。
（若基线红恰好落在 provider|model|runtime|session|interpreter 过滤面且根因在白名单内，
升级为修复项并记录。）

## D-6 OOM"真实层级"的判定口径
真实=在本机可控资源约束下触发逐级降档且降档有可观测痕迹（VRAM 配额用 fake backend 的预算记账+
真实分配路径；CPU 内存阶梯用测试专用小图/预算参数触发 planTileMemory 阶梯降档——配额是真实的
进程内记账而非模拟注入）。显存硬件级 OOM 本机无 GPU，不可真实触发——保留注入测试并标注"模拟"。
（口径依据 ADR 0130 D4/D5：预算记账失败路径本身就是生产路径。）

## D-7 候选缺陷的否决记录（避免伪造缺陷/同义反复）
1. **completedTiles "先计数后消费"不是缺陷**：chunk_graph.h:26 明示 "every payload that LEAVES
   the final queue bumps completedTiles"，且 ChunkPipeline 家族语义一致（pipeline 同样在 consumer
   调用后、abort 判定前计数）。本轨道以测试钉桩该文档语义，不改实现。
2. **pad/class_mapping 重复检查消息不去重**：两处同因消息（kMaxPreprocessPad/:784 与
   kMaxPreprocessPadPx/:1010；class_mapping :1078 与 :1093）的文本被既有测试钉桩
   （test_model_catalog_v2.cpp:1126、test_tile_semantic_equivalence.cpp:269 等），去重的收益
   （消息去噪）低于破坏既有契约钉桩的风险。记录否决。
3. **BoundedWriteGate 超量 release 钳 0 保持宽容**：RAII 在异常路径可能叠加释放，typed 化会把
   无害容错变成生产崩溃；钉桩现行为并在此记录接受理由。

## D-8 crash-orphan 采纳决策（WP-D"二选一"）
ProductPublishGuard 的采纳是**字节级 rename 恢复**（不校验 sidecar 内容完整性）：被中断采纳的
重入窗口里，若 final 路径已有 stray sidecar 且 backup 也有 sidecar，**backup 对自己的 sidecar
胜出**（POSIX rename 覆盖语义，确定性）。截断/损坏的 sidecar 会被原样恢复——消费侧
provenance_verify 是内容真值门；下一次成功发布重写之。理由：guard 是 rename-only 快路径，
不引入 I/O 读取语义。

## D-9 master 预存构建断裂的最小解锁（白名单边界例外，逐条记账）
本轨道基线构建在 master `15e5c66b5` 上无法完成（#1335 在修的 P0 类），阻塞全部验证。按既有
轨道先例（r2/r3/perf-observatory 均有"master 编译断裂 en route 最小修复"先例）做最小解锁：
1. `src/agent/CMakeLists.txt`：sicnu_agent 的 PUBLIC 链接表补 `sicnu_agent_loop`——
   agent/tools/agent_session_adapter.cpp 消费 VerificationReport::aggregate（定义在
   agent_loop/session_seams.cpp:157），链接缺失导致**所有**链接 sicnu_agent 的可执行目标
   undefined symbol/DSO 失败（含本轨道全部 sicnu_add_test 目标）。语义零改动。
   该文件在白名单字面之外，属"白名单外改动记账"条款下的显式例外，PR 正文披露。
2. 未修（记录）：test_lab_data_pack（缺 Qt include，#1335 域）、test_band_role_combo /
   test_raster_layer_combo / test_roi_statistics_widget / test_spectral_profile_widget
   （gdal_dataset_wrapper 引用 fault::shouldFail 但目标未链 sicnu_runtime——#1335 链接图域）、
   test_workbench_enum_provider（crs_selector）。全部不在本轨道过滤面与白名单内。


## D-10 提交数下限未达的说明（诚实记录）
3.2 下限要求 ≥16 个原子提交；本轨道落地 10 个（+1 工件提交 = 11）。原因：TDD 节拍中多个
WP 的 RED→GREEN 在同一轮内完成（缺陷定位快于估算，例如 chunk typed 化与 gpu 诚实性各 1 个
提交即覆盖），且最终一次 review remediation 合并了 5 个修复面。每个提交独立可编译、职责单一；
提交数低于下限不等于工作量低于下限（65 用例 vs 18 下限、OOM 6 真实档 vs 3、provider 4 格 vs 3）。

## D-11 token 消耗低于 3.2 算术锚定的说明
3.2 算术预估 ~250M（含大量多轮回读与上下文重建）。实测 ~62M：主因是①后台 make/ctest 的
等待期不消耗模型上下文（仅轮询式 Bash 调用）；②只读 subagent 压缩了源码阅读量；③多个 WP 的
RED 在首轮定位（SIGFPE/SEGFAULT 一眼可判）。已按 goal-loop 账本规则逐轮记录，可审计。

## D-12 Qt6 QFile::rename 拒绝覆盖（平台事实）
本机 Qt6 实测（探针程序）：rename 到已存在目标返回 false 且目标保持原内容——非 Windows-only
行为。所有"恢复/替换"路径必须先 remove 目标再 rename（已在 model_publish.cpp 三处落地）。

## D-13 Round 2 范围决策（PR #1353 合并后的证据驱动重入）
PR #1353（本轨道 Round 1 全部交付：65 用例/P1-8 四元组/OOM 6 真实档/provider 4 格/crash-orphan
fence/python 通道）已于 master `6ff7aabd81` 合并；#1334–#1354 整波 R4 均已合并。Round 1 各 WP
前提已消耗，按"Phase 0 实测前提不成立→记录证据后收窄，不要为了做而做"条款不重做。Round 2 范围
= 新 master 实测缺口 + Round 1 遗留项中白名单内者：①master 自身构建/测试断裂收口（见 D-14/D-15）
；②遗留项#4（buildTileGrid 异常族恢复，D-15）；③遗留项#5（python 池真实恢复路径端到端驱动，
替代 Round 1 的手工回调验证）；④过滤面基线红测中文件归属在本轨道白名单内者。

## D-14 qt_lifecycle.h 一行解锁（白名单边界例外，D-9 先例）
`tests/support/qt_lifecycle.h:48`（#1342 提交 e4b3d245df 引入）：`QCoreApplication *asApp =
qobject_cast<QApplication*>(existing)` 把派生类指针存入基类变量后 `return asApp`——g++ 下
invalid conversion 硬错误，打穿全部 teardown 族测试 TU（dual_viewport/layer_sync/timeline/
retirement_gate/d17_workflow 等）**以及本轨道白名单内、过滤面内的 test_provider_http**。修复 =
变量声明改为 `QApplication *asApp`（qobject_cast 结果本就是 QApplication*，零语义变化）。
该文件在白名字面之外，按 D-9 先例记为显式例外；PR 正文披露并知会 #1342 域。

## D-15 chunk 契约双钉矛盾的和解（经对抗 review 修正方向，最终裁定 #1056 契约）
初始判断：master 上 test_chunk_contract_11 的 3 红（length_error）是 #1056（3070d3e1ad）改 impl
未同步旧测试所致，遂在 aaec6433b5 把 impl 恢复为 length_error。对抗 review（P0-1）推翻：单行
grep 漏掉了多行格式的 test_chunk_graph.cpp:88-105——#1056 自己在同一批拒绝路径上钉了 4 个
overflow_error（master 绿）。即 master 同时存在两个矛盾钉子（contract_11 旧契约红 /
chunk_graph 新契约绿），翻转 impl 只是把红换了个文件。最终裁定：尊重 #1056 的蓄意契约变更，
impl 保持 std::overflow_error（tile_spec.h/tile_run_contract.h 恢复与 master 字节一致），更新
contract_11 的三处陈旧断言并注明和解缘由（ac26c2a86a）。裁决依据：#1056 是蓄意的 typed
arithmetic 契约 overhaul（impl+新钉子一体交付），contract_11 是被遗忘的陈旧断言；src/ 无任何
catch 站点依赖两种类型（review grep 实证）。contract_11 在白名字面外，按 D-9/D-14/D-16 先例
披露。教训记入账本：断言类型全库核查必须容忍多行宏格式。

## D-16 test_gpu_plane stale-eviction 双改并集修复（#1353 合并拼接产物，白名单例外披露）
基线红 `GPU plane evicts stale model identities`（0==1）定性：#1353 合并把同一测试块的两个正当
改写做了并集——master 侧 b42e92a1f0 钉 #1094 use_count 门（held 不回收/drop 后回收两段式），
本轨道 Round 1 的 a9b10e7928 在首次 evictStale 前丢弃句柄。并集后测试既丢了句柄又期望 held
语义，必然红。impl 在 ed762cc1b8..a726d17a6 净差为零（git diff 实证），唯一牺牲品是测试文本。
修复=保留 held/drop 两段结构、删除矛盾的 pre-eviction reset（ef263ce196）。文件在白名字面外，
按 D-9/D-14 先例披露；替代方案（改 impl 迁就拼坏的测试）会废掉 #1094 门，不可取。

## D-17 publish 占位 fence 生命周期缺陷（Round 1 自身实现的真缺陷，本 Round 核心 P1）
Round 1 的 PublishFenceSlot 是构造器局部 RAII：构造器一结束就擦掉全局 fence key——fence 只
覆盖构造器窗口，guard 存续期（park + 慢发布 + swap）全程无防。类注释宣称"guards the WHOLE
guard lifetime"，实现与之相悖；既有并发用例靠时序运气通过（loser 的构造恰落在 winner 构造窗口
内），基线在高负载宿主上翻红暴露了缺陷。修复=PublishFenceSlot::release() 在构造器两个成功出口
把 key 所有权移交 guard 的 m_fenceHeld；全部 throw 路径仍由 holder 兜底（throwing ctor 不跑
析构），无泄漏。用例侧握手补 fenceHeld 边实现确定性（419d1e2ad1）。证据：10/10 连跑绿 +
全套 87 断言绿；修改前握手修正版 1/10 绿、基线 1 红。
## D-18 独立对抗 review 的处置（1 个只读 subagent）
判定 BLOCK → 全部处置：P0-1（chunk_graph 遗漏，见 D-15 与 ac26c2a86a）；P1-1（规划工件未提交，
本提交补齐）；P2-1（rig 崩溃窗口余量 sleep 2→6s，false-red 风险消除，随 ac26c2a86a）。抽查
通过项：fence 所有权移交的全部 throw 路径覆盖与 m_fenceHeld 不变式、恢复用例的线程模型与
answeredCount 恰一次约束、白名单例外最小性、其余 5 提交 message 与 diff 相符。
