# ACCEPTANCE — R35-T02 首批切片（WP01 + WP18 + WP05）

worktree：exp-rs-r35-t02-model-services-20261118T155435Z；base=120677e6（origin/master）。
构建：dev-default preset + 共享 OpenCV5（见 DECISIONS#10），Debug，双 resource_guard 锁，-j4。

## RED（pristine HEAD + 新测试，2026-11-19 01:1x 实跑）

| 测试 | 结果 | 关键证据 |
|---|---|---|
| test_r35_t02_python_worker_deadline | 2 failed / 2 passed | trickle：`elapsedMs <= 5000` 展开为 `8084 <= 5000` FAIL——1200ms deadline 被 8s trickle 绕过（真实 QProcess）；stderr case 断言位置修正（thrown msg 不含 stderr，改查 health().lastError） |
| test_r35_t02_ensemble_acquire_cancel | 1 failed | `waitUntil(ensembleDone, 5000)` = false——父取消后 ensemble 卡在 in-flight waiter（无 cancelPredicate） |
| test_r35_t02_provider_manifest_typed | 18 assertions failed | 坏 token 12 项 issues 为空（toInt(default) 无声通过）；程序化 -1000/-8 acquire 成功返回 session、error 为空 |

## GREEN（应用三 patch 后，2026-11-19 01:5x ctest 实跑，双锁 -j1）

`ctest -C Debug -j1 -R 'R35-T02|…回归 case…'`：**28/28 passed, 0 failed, 0 skipped**（FAST label 55.86s）。

| # | case | 结果 | 耗时 |
|---|---|---|---|
| 245 | R35-T02 WP01: trickle hits absolute read deadline | Passed | 2.15s（RED 8.08s） |
| 246 | R35-T02 WP01: legal slow segmented frame succeeds | Passed | 2.08s |
| 247 | R35-T02 WP01: fully silent worker fails at deadline | Passed | 2.04s |
| 248 | R35-T02 WP01: stderr chatter does not stall deadline | Passed | 2.08s |
| 249 | R35-T02 WP05: provider knobs typed at catalog | Passed | 0.24s |
| 250 | R35-T02 WP05: programmatic knobs domain-checked | Passed | 0.92s |
| 909 | R35-T02 WP18: parent cancel exits member acquisition bounded | Passed | 1.14s |

回归（同一 ctest 会话）：test_provider_python 15 cases（known answer/poison/crash-restart/caps/#1056 并发探测/interpreter 安全×4/malformed/garbage-crash-once/#1384/wire classification/#r18B write-deadline）+ test_ensemble_parallel 7 cases（bit-identical/admission ceiling/8-run ledger/member failure/mid-run cancel/sidecar rollback/publish swap fault）全部 Passed（21/21）。

## oracle 对照

- WP01：trickle 有界容差内失败（2.15s 含启动 ≈ deadline+容差）；合法慢分段帧成功；完全静默超时；stderr 活动不拖延（lastError 带尾巴）；失败后 permanentlyUnavailable + 二次调用拒绝 + 新 fake session 已知答案恢复。✔（worker-exit 路径由既有 test_provider_python crash 用例覆盖：Passed）
- WP18：固定 barrier（阻塞 factory + peer waiter）；父取消 5s 内退出（实测 1.14s 总时长）；后续成员 c 不启动；peer waiter 与 retry 拿到 session。✔（不可中断 factory lane 已在代码注释与测试注释中标清）
- WP05：缺失/null→默认、0→默认、合法值往返；字符串/bool/小数/负值/超 int/数组/对象全部带字段路径拒绝；程序化负值进程启动前拒绝、0=默认真实 forward=9.0。✔

## 未执行（如实）

- Windows/真实 ORT/GPU lane：本机无环境，NOT_EXECUTED。
- 未跑全库其余 1335 个测试（改动影响面之外），按协议仅回归直接受影响目标。
