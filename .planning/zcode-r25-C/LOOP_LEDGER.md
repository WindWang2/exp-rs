# LOOP_LEDGER — R25-C

格式：第N轮 | 改动 | 验证结果（命令+exit code） | 通过/未通过 | 下一步

- 第0轮 | W0 侦察：fetch/master=2610cb4741、0 open PR、11 候选 issue 全 OPEN 实查、worktree 建立、基线构建启动（后台 exec_2d2f1341） | BASELINE.md 写入 | 通过 | W1 契约测试先行（RED）
- 第1轮 | W1：fake server 加 --version-file/--tools-file/--die-if-exists；新契约测试 r25_c_generation_drift（RED 3/4）→ mcp_bridge.ts 实现 serverInfo/protocolVersion/generations getter + onGeneration 钩子 + 8 类 bridgeError 类型码 → GREEN（单文件 4/4；全套件 45/45 ×2，第二次 45 pass 0 fail） | node --test pi/test/ exit 0 | 通过 | 提交 6bf14aaf9a；顺带修复 r18_c timeout 测试预存读写竞态（bounded poll），id_null flake 记录为预存（基线 2/8 复现，单跑 0/25）
- 第2轮 | W2：定位 algorithmEntryMap 丢弃 prerequisites/limitations（descriptor 有真值：rs:spectral_index 等）；RED 测试 tests/test_r25_c_capability_honesty.cpp（未实现 → 编译期 RED）；实现 entry 投影 + guarantees | 待构建验证 | 进行中 | 构建后跑 GREEN×2
- 第2轮附 | W3：定位恢复诊断缺口（重启后 run_id 无查询面、recoverAtStartup 丢弃恢复的 runs）；RED 测试 test_r25_c_workflow_resume_diagnostics.cpp；实现 coordinator.runForRunIdReadOnly（只读、不取锁）+ get_workflow_status run_id 分支 + resumable advisory + schema 更新 | 待构建验证 | 进行中 | 构建后跑 GREEN×2 + snapshot 再生
