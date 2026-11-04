# LOOP_LEDGER — R25-C

格式：第N轮 | 改动 | 验证结果（命令+exit code） | 通过/未通过 | 下一步

- 第0轮 | W0 侦察：fetch/master=2610cb4741、0 open PR、11 候选 issue 全 OPEN 实查、worktree 建立、基线构建启动（后台 exec_2d2f1341） | BASELINE.md 写入 | 通过 | W1 契约测试先行（RED）
- 第1轮 | W1：fake server 加 --version-file/--tools-file/--die-if-exists；新契约测试 r25_c_generation_drift（RED 3/4）→ mcp_bridge.ts 实现 serverInfo/protocolVersion/generations getter + onGeneration 钩子 + 8 类 bridgeError 类型码 → GREEN（单文件 4/4；全套件 45/45 ×2，第二次 45 pass 0 fail） | node --test pi/test/ exit 0 | 通过 | 提交 6bf14aaf9a；顺带修复 r18_c timeout 测试预存读写竞态（bounded poll），id_null flake 记录为预存（基线 2/8 复现，单跑 0/25）
- 第2轮 | W2：定位 algorithmEntryMap 丢弃 prerequisites/limitations（descriptor 有真值：rs:spectral_index 等）；RED 测试 tests/test_r25_c_capability_honesty.cpp（未实现 → 编译期 RED）；实现 entry 投影 + guarantees | 待构建验证 | 进行中 | 构建后跑 GREEN×2
- 第2轮附 | W3：定位恢复诊断缺口（重启后 run_id 无查询面、recoverAtStartup 丢弃恢复的 runs）；RED 测试 test_r25_c_workflow_resume_diagnostics.cpp；实现 coordinator.runForRunIdReadOnly（只读、不取锁）+ get_workflow_status run_id 分支 + resumable advisory + schema 更新 | 待构建验证 | 进行中 | 构建后跑 GREEN×2 + snapshot 再生
- 第3轮 | R1 独立评审（子代理，未参与实现）：P0=0 P1=0 P2=2 P3=9。RED→GREEN 真实性经子代理在 origin/master 源上复跑验证（4/4 RED）。逐项修复：P2 onGeneration 异步拒绝、P2 exprs_status 在飞诚实性、P3 错误类保留/SIGKILL 孤儿/路径披露/checkpoint_present 探测/supportsCancellation/正则锚定/死代码/测试补齐。E2E 调试中发现并修复真实产品缺陷（unref SIGKILL 不触发 → 孤儿 host 持锁阻塞 resume；测试自身 stop() 缺失）。 | 提交 41425da645；pi 46/46×2；C++ 全回归绿（198+72+9962+223+163+194+115+21+17866+78+46+7+17+24031 断言） | 通过 | R2 复审
- 第4轮 | R1 修复后回归全绿；更新 TEST_RESULTS | — | 通过 | R2 独立评审
- 第5轮 | R2 独立评审（新上下文子代理）：P0=0 P1=0 P2=3 P3=7。R2 实测 pi 46/46 与 C++ 198+72，并用 strings/nm 证明二进制含修复代码。修复：P2-1 killNow 死代码（exit hook 循环顺序）、P2-2 workspace=/ 时 root+"/" 变 "//" 导致路径坍缩（改用 containment::canonicalPathWithin）、P2-3 R1 五修复无判别性测试（补 3 个 RED-able 测试并逐一 revert 验证：\z 判别 +1 失败、pass-through 路径 +1 失败、恢复后绿）；P3 \Z→\z（PCRE2 尾随换行）、provider supportsCancellation。 | 提交待；pi 48/48×2；C++ 95+198+9962+223+163+193+115+21+17866+78+46+7+17+24031 断言全绿 | 通过 | PR 前最终检查
