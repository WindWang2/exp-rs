# R23-B DECISIONS

1. **W1 证据形态：stub-compile gate**。本机与所有 CI lane 均无 OpenVINO/TensorRT SDK；option-OFF 文件零编译覆盖正是回归根因。新增 ctest（sh 脚本 + tests/fixtures/provider_stubs 最小 API 面头 + `-fsyntax-only`）是唯一可离线常态化的编译门。stub 版本钉 TRT 10.x（v10 API 分支）；OV 为纯值语义最小面。
2. **W2 触发条件 `!heads.empty()` 而非 `headsDeclared`**：测试与部分调用方直接构造 ModelInfo（r21-B 测试即填 heads 不设 headsDeclared）；以向量本身为准更符合语义。旧扁平清单（无 heads）保持历史放行，兼容性优先。
3. **W2 logit+labels 放行**：argmax 对 logits 保序（softmax 单调），拒之会误伤合法清单；mask/confidence 消费概率值本身，logit/distance 一律拒；distance 反序 argmax 也拒。
4. **W3 走 `upsertRunWithMetric` 新 store API 而非导入器双连接/手写 SQL**：单一 authority（ExperimentStore 持锁+事务），复用 writeRunInTxnLocked 与抽出 writeMetricInTxnLocked，诊断码与既有完全一致；导入器补偿腿删除（事务原子性取代）。
5. **W3 runId 一致性前置校验新增**：metric 名与 run 不一致时原子性承诺无意义（该行不属本事务 run），typed 拒绝（experiment.invalid）。
6. **W4 证据局限如实声明**：离线环境无法构造"公网首跳+私网重定向"判别用例（策略开启时本地 stub 在首跳即被拒——master 与修复后行为相同）；修复为传输统一（probeRemote → httpFetchStatus 受控传输，per-hop guard+DNS pinning 由 #1456/#1485 既有传输测试覆盖），GREEN 证据=既有 probe 契约族全绿 ×2。
7. **W4 头优先级**：Content-Range 证明 range 支持的优先级高于 Accept-Ranges: none（206 答案是最强证明；accept-ranges 头不再覆写）。
8. **不修 #1558**（共享 e2e 恒真断言）：断言域属 recipes/tasks 他轨写集；B 轨不改正文共享测试文件。
9. **构建串行**：全局 flock + -j2；批量目标一次持锁全建，减少锁往返。
10. **#1491**：本机实跑替代线上判断；线上 CI 无完成 run，环境性归因记录而非臆断源码缺陷。
