# BASELINE — R5 Track 07: model runtime / Python worker hardening

## 执行时事实（2026-09-28，非规划快照沿用）
- `origin/master` = `a726d17a6224632d929e782e996351732632f272`（= 规划快照 SHA，fetch 无漂移）
- 开放 PR：0；三个并行 R5 分支（workflow-durability / persistence-consistency / ci-test-budgets）均停在 master commit，无提交体
- 开放 Issues 1355–1364：无 model runtime / python worker 直接项
  （#1358 PipelineRunCoordinator teardown → 其他 Track；#1362/#1364 exprs plugin → Track 03）
- 种子 PR：#1353（R4 model runtime 收口，2026-09-27 合并），其 REVIEW_LOG/PR-body 明示残留：
  1. in-flight 恢复用例仅手工回调驱动，未走 pool 真实恢复路径
  2. fence key = QDir::cleanPath：无 relative/absolute 归一、无 symlink 解析、无 case 处理
  3. `test_chunk_contract_11` 期望 `std::length_error`，`buildTileGrid` 实抛 `std::overflow_error`
     （test_chunk_graph 两处 + tile_spec.h 注释均钉 overflow_error → 契约 = overflow_error，pin 过期）
  4. 9585a895cc 提交信息声称 "ensureSignalsConnected() re-arms stderr capture"，
     但 diff 仅改 stopWorker 注释 —— readyReadStandardError 连接在 stopWorker() blanket
     disconnect 后永久丢失（复用实例的 stderr 捕获与崩溃归因失明）

## 工作区
- worktree：`/home/kevin/project/exp-rs-r5-model-runtime-20260928-002714`
- 分支：`hardening/r5-model-runtime-workers-20260928-002714`
- 构建：`build-dev/`（Debug / ENABLE_TESTS=ON / CMAKE_PREFIX_PATH=/home/kevin/pwb-sdks/root/usr，
  cmake=/home/kevin/toolchain/cmake-dist/bin/cmake 3.30.5，系统 GCC 16.2.1；本机无 gcc-15）
- 并发硬约束：`--parallel 2` / `ctest -j1`（压力与进程树用例 RUN_SERIAL）/ QT_QPA_PLATFORM=offscreen

## 缺陷清单（inventory 后认定，逐项有 oracle）
| # | 域 | 缺陷 | 根因定位 |
|---|---|---|---|
| D1 | python worker | stderr 捕获重武装缺失（#1353 残留，声称已修未修） | python_worker_process.cpp ensureSignalsConnected |
| D2 | python pool | 干净退出（exit 0/1）+ in-flight 请求 → 无 workerCrashed → 调用方永久挂起 | onProcessFinished 白名单 + pool 只连 workerCrashed |
| D3 | python pool | 协议 EOF（socket 断开、进程存活）→ in-flight 挂起 | server clientDisconnected 无 pool 侧处理 |
| D4 | python pool | 重放 sendRequest 返回 -1（no client）时静默丢弃恢复请求 → 挂起 | handleWorkerCrash replay lambda |
| D5 | python pool | shutdown() / setPoolSize 收缩 直接删 server，in-flight 回调泄漏不答 | shutdown/shrink 路径 |
| D6 | python pool | failPendingRequests 全部误报 "Worker crashed; retries exhausted"（restart-failed / node-removed / shutdown 各异） | 错误消息不区分原因 |
| D7 | publish fence | fence key 仅 cleanPath：relative vs absolute vs symlink alias 各得独立 fence → 双发布互踩 | model_publish.cpp g_publishFences |
| D8 | publish fence | DetectionPublishGuard 与 ProductPublishGuard 同型 adoption/park 机制但完全无 fence（#1353 只给 Product 加了） | model_publish.cpp |
| D9 | provider | malformed JSON 响应 → "worker exited unexpectedly"（进程其实活着），流错位后续请求连锁误诊；worker 不停机 | readLine ReadOutcome::Failed 合并 |
| D10 | provider | handshake failed 分类为 Unknown（应为 ProviderCrash） | classifyInferenceError 词表 |
| D11 | chunk contract | test_chunk_contract_11 pin 过期（length_error vs overflow_error，master 既有红） | 测试 pin |
| D12 | python pool | 崩溃预算 5 次永久耗尽，长寿命进程无法挣回预算（dead worker recycling 缺失） | restartCount 单调 |

## 不碰范围
- MCP/QObject 全局 teardown（Track 01）、workflow checkpoint 状态机（Track 06）、plugin host worker（Track 03）
- 不新增 provider/模型格式/推理 UI；worker_daemon.py 协议面不改（fake worker 走测试 fixture）
- gpu_plane ModelSessionPool（#1353/#1331 已收口，无新证据不动）
