# PLAN — R5 Track 07: model runtime / Python worker hardening

## 目标
Model runtime / Python worker：进程池恢复（真实重启路径）、发布 fence（平台真身份）、
路径 canonicalization、错误契约收口。不加新模型/provider。

## 工作分解（WP → 缺陷 → oracle）
| WP | 缺陷 | 修复 | oracle |
|---|---|---|---|
| WP-A worker process | D1 stderr 捕获重武装缺失（#1353 声称已修未修） | lambda → 成员槽 onReadyReadStderr；ensureSignalsConnected 重武装第 3 连接 | 既有 r4 stderr 用例 + 池 crash reason 携带 marker（SIGKILL 用例） |
| WP-B pool recovery | D2 干净退出/EOF + in-flight 挂起；D3 重放 -1 静默丢弃；D6 错误消息误报 | bindNodeSignals（crash/finished/EOF 三入口）→ handleWorkerLoss 统一恢复；replayRecoveredRequest（sendRequest 返回 id/-1） | test_runtime_pool_recovery_r5：SIGKILL 重放、干净退出恢复、EOF-存活恢复+预算终止、watchdog、resize、shutdown |
| WP-C pool lifecycle | D5 shutdown/resize 泄漏 in-flight；D12 预算永久耗尽 | shutdown/resize 先 takeInFlight+typed 答复；crashBudgetLeft 挣回/退役 | retirement（5 崩溃退役）与 recycling（挣回后 7 崩溃存活）两个真实重启用例；shutdown/resize 答复断言 |
| WP-D publish fence | D7 键=cleanPath 不识别 alias；D8 DetectionPublishGuard 无 fence | canonicalPublishFenceKey（绝对化+深层存在祖先 symlink 解析+探测式大小写折叠）；PublishFenceLease 统一两 guard（首成员 RAII） | test_model_publish_fence_r5：多拼写单 fence、异文件互不阻塞、throwing-ctor 跨拼写释放、detection fence、双线程单赢家、case 探针与 stat 真值一致 |
| WP-E provider contract | D9 malformed→"exited unexpectedly"误诊+流错位重放；D10 handshake→Unknown | ReadOutcome::Malformed（fail-closed：permanentlyDead+stopWorker+typed OutputInvalid）；ProviderCrash 词表加 "handshake failed" | py_worker_garbage.py 真进程：malformed 消息/分类/错误码/health/二次前向拒绝；分类 pin 套 |
| WP-F chunk contract | D11 length_error pin 过期 | pin 改 overflow_error（与实现、test_chunk_graph、tile_spec.h 注释一致） | test_chunk_contract_11 全绿（master 既有红） |
| WP-G master 解锁 | 基线 tests/CMakeLists.txt:14728 残留 `=======`（7bb6398c0 引入） | 删一行 | cmake configure 成功（本 Track 与其他 Track 的前置） |

## 执行序
1. 刷新事实 → worktree → inventory（BASELINE.md）✅
2. WP-A/B/C（python worker/pool）+ fake worker fixture ✅（代码）
3. WP-D（publish fence）✅（代码）
4. WP-E/F/G ✅（代码）
5. 构建（-j2）→ 逐套运行 → 修复 → 回归 ✅（进行中）
6. 独立 review subagent → 自修复 → 复验
7. fetch + 同步 origin/master → 重放冲突 → 复跑受影响测试
8. commits + push + PR

## 资源纪律
- 全部构建 `--parallel 2`；ctest `-j1`；进程树/压力用例 RUN_SERIAL（TIMEOUT 300）
- 不等待线上 CI；验证以本地可重复运行为准
