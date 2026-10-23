# R20-03 LOOP_LEDGER — goal-loop 轮次账本

GOAL: 修复/补全 Pi/MCP harness、工具契约、workflow 与插件执行闭环域内有实证的缺陷，双绿 + 双评审 + PR。
Oracle: 任务书 §8（真实 RED/GREEN×2、快照三腿一致或如实 KNOWN_BASELINE_FAIL、不另造 agent 框架）。
会话轮上限：15。每轮一类根因。

## 轮 1 — LIVE RECON
- origin/master=28e1891390（与快照一致）；无 open PR；126 open issues 全量拉取。
- pi Node 套件 39/39 GREEN（glob 形式；Node24 目录形式失效，记入 TEST_RESULTS）。
- 发现 R20-01/R20-02 并行轨实时构建中；vendor 前缀消失 → 复刻 R20-02 纯系统依赖配方，configure exit 0。
- worktree + 分支建立；BASELINE/ISSUE_TRIAGE/OWNERSHIP/TASKS 落盘。

## 轮 2 — 分诊 + 双路深扫（只读）
- 任务书点名种子全部现码验证：#1539/#1540/#1552/#1589/#1594/#1592/#1654 及 #1628-4/#1629-3 等 → 全部 ALREADY-FIXED（live 复核证据入 ISSUE_TRIAGE）。
- 两路并行子代理深扫（agent/harness/tool_catalog + workflow/sdk/plugins/preflight/science_context）→ 新缺陷 D1-D8（全部本轨写区）+ 跨区 D9。
- R12（R19-02 遗留）分析：ExecutionHandle::cancel ≡ TaskCenter::cancelTask 同路径 → 无行为差异 → 无法造红，DEBT。

## 轮 3 — TDD 实施（W1-W5）
- W1 IPC result 门：RED 测试 + 修复（ipc_envelope.cpp）。
- W2 sidecar 加载器（3 文件）：validator 级 + loader 级 RED 测试 + 类型门修复；knowledge validateEntry 加 extends 门。
- W3 decision_record：RED 测试 + optionalString 门卫。
- W4 TooLarge hexdump：RED 断言（salvaged 前缀）+ 抢救修复。
- W5 死字段 approvePendingRepair 删除（grep 零引用实证）。
- 构建：configure 完成；全量构建在全局锁队列中等待 R20-01（flock 串行，符合协议）。
- RED 验证策略：构建可用后先以 `git stash` 退回未修 src 复跑新测试取 RED，再恢复取 GREEN×2（同一二进制环境）。
- 提交拆分：tests-only（8721548ca1，即 RED 态）→ fix(sdk) 0357f62d47 → fix(harness) 9b3281387f → fix(agent,ops) e463f711a1。

## 轮 4 — Review R1（fresh 对抗式，构建等待期先行）
- 结论 READY-WITH-FIXES：P1×2（isIntegral 放行超 int32 范围值仍抛；catalog reload 非对象文件非 const id 索引仍抛）、P2×2（W1 注释机理不准确；decision_record 测试未钉 errorCode）、P3×7。
- 全部修复于 017b0a9f66：isInt() 门 + 非对象 skip + 钉子测试（3000000000/4294967295.0/array 文档）+ 注释改写 + typedRefusal 断言 + RAII 单例恢复 + 缩进。
- 评审正面确认：全库 157 sidecar + 15 knowledge + relations 零错型（新门不破坏现存数据）；所有 in-tree sendResponse 生产者发对象；范围合规。

## 轮 5 — 构建推进与自陷修复
- R20-01 构建败于其自身 test_sar_kernels 命名空间错误（非环境）；R20-02 接锁全量构建；R20-04/05 后续加入队列。
- 本轨首轮定向构建 34 目标：21 个成功后 stop 于 test_plugin_host（目标在 if(SICNU_EMBED_PYTHON=OFF) 内合法不存在）→ 剔除后 11 目标重排。
- compile_commands -fsyntax-only 预检抓到 1 个真编译错（Json::Value(3000000000LL) 歧义）先于占锁修复。
- HEAD 预览跑 test_exprs_ipc 抓到 W4 别名缺陷：既有测试把 error 同时当 payload 出参 → 前缀抢救覆盖 E6003 消息。修复=源码先抢救后写 error（别名时消息契约优先）+ 测试去别名（033c0ab154）。
- 自陷修复：排队命令误用 git stash 会摘走未提交修复且脚本 PHASE3 checkout HEAD -- src/ 会丢弃工作树改动 → 终止、pop、提交、重排。
- pi Node 套件 HEAD：39/39 ×2（首跑 38/39 为双构建重载 flake，复跑两次全绿）。
- 17 个不依赖未建二进制的 family ×2 完成：27×RC=0；test_surface_parity/e2e 各 1 败=**缺 sicnu_geo_rs_cli/surface_mcp_host 产物**（waitForStarted false，非代码回归——补进 PHASE1）；snapshot registry 腿 1 败=CT-6 预期（首个失败工具 cartography:chart_list）；r14_C run1 通过后段错误（run2 RC=0，疑似负载相关，待复跑判定）。

## 轮 6 — Review R2（第二 fresh，全范围+失败矩阵）
- READY-WITH-FIXES：P1×1（variants[].when.param 抛穿门+静默空目录）、P2×1（查询时 variantParams 数组腿）、P3×2（where 裸前缀跨文件误配；测试注释残留）。
- 全部修复+RED SECTION 于 88a637df6b；语法预检通过。
- R2 显式核验五项全过（isInt 矩阵、optionalString 矩阵、剩余未门控读清点、测试别名、W1 wire 形状）+W5 聚合初始化排查。

## 轮 7 — 整合周期执行 + 最终验证电池
- 周期三阶段全成：PHASE1 plugin 11 目标+exprs_ipc 重链（sed 未生效漏掉 cli/host，后续单独补建）；PHASE2 master-RED（exprs_ipc 2 断言/capability 12 断言 3 例/harness 1 例/agent_ops 0=结构性）；PHASE3 HEAD-GREEN 确认。
- capability 残留 2 败分诊：:206 drift=**r18-a #1663 漏再生 sidecar**（master 预存）→ gen-meta 157 sidecar 单文件 diff + gen-pages 页面再生 → 17 例/1637 断言全绿 ×2；:647=我 R2 测试条目用了非 knownKeys 的 "note" → 改 intents（c757c2492f）。
- 最终电池：exprs_ipc/harness/agentops ×2 RC=0；parity 12/5506 ×2、e2e 4810+4674 ×2（cli 140MB+host 补建后）；r14C ×2 绿；pi 39/39；plugin 族 6 绿。
- **master 对照周期**：host_process :1259 与 boundary/halffail/unload_order/loader 四测试 master==HEAD 失败集逐字节相同 → 全部预存红归档（KNOWN_BASELINE_FAIL）。
- CT-6 证据：snapshot registry 腿 ×2 确定性失败，首工具 cartography:chart_list。
- .gitignore 白名单 append（markdown-only 惯例）+ 12 账本文件入库。

## 轮 8 — 交付
- 12 commits（10 代码/数据 + 2 文档）；推送前 fetch 复核 origin/master=28e1891390 不变；与 open PR #1668/#1669 零文件重叠。
- PR 创建成功并复核：https://github.com/WindWang2/exp-rs/pull/1670（OPEN，非草稿，head 06ed6f6df393f5c638addfab334a5500bce4151b）。
- 最后一条账本记录：任务以 PR 提交为交付边界，不自动合并、不等 CI。
