# R20-03 FINAL_REPORT — Pi/MCP Harness、工具契约、Workflow 与插件执行闭环

## 分支与基线
- Branch: `agent/r20-03-pi-mcp-workflow-20261022T160809Z`（独立 worktree `/home/kevin/projects/rs-studio/exp-rs-agent/r20-03-pi-mcp-workflow-20261022T160809Z`）
- BASE `28e1891390` = live recon 时 origin/master（fetch 复核；推送前再次 fetch 确认）
- 12 commits：4 修复 + 1 RED 网 + 3 评审修复轮 + 1 sidecar/页面再生 + 3 文档
- **PR #1670**: https://github.com/WindWang2/exp-rs/pull/1670（OPEN 非草稿，head 06ed6f6df3 = 推送前 fetch 复核的 origin/master 28e1891390 之上；不等待 CI、不自动合并）

## 头条成果
1. **W1 [P2]** IPC Response `result` 类型门：worker→host 不可信帧的非对象 result 此前直达宿主的非 const `operator[]` 消费点（resolveReference：异常构建抛穿无 catch 线程/无异常构建 abort）。decode 时镜像 params 门拒绝（E6002）；null/absent/object 词汇表与 params 一致。master-RED 实证（非对象 result 被接受）。
2. **W2+R1+R2 [P2] capability 加载器 fail-closed 闭环**（catalog/relations/knowledge 三加载器）：错型 schema_version/id/family/kind/extends/variants.when.param/非对象文档全部改为记名问题只损失该文件；master 上一文件一字段类型错 = 整个 reload 中止 + GUI/copilot 直连路径进程死 + **knowledge 目录静默清空**。isInt 范围门（R1）、非对象 skip（R1）、查询时 variantParams 调用方输入门（R2）、精确 where 前缀（R2）。
3. **W3 [P3]** decision_record 七参数类型门（typed INVALID_PARAMETER，直接 dispatch 路径不再异常穿透）。
4. **W4 [P3]** TooLarge 诊断抢救 4 字节长度前缀（channel "stream head" hexdump 此前恒空）；含别名调用方安全序（先抢救后写 error）。
5. **W5 [P3]** `OpsRunRequest::approvePendingRepair` 死字段删除（#1539 修复后零读零写）。
6. **跨轨修复**：rs:sam_classify sidecar drift（r18-a #1663 漏再生）→ gen-meta + gen-pages 闭环，drift 门 master 红 → 全绿。
7. **验证债清偿**：plugin_host 进程族 11 可运行测试全部补跑（R19-02 NOT_EXECUTED）；6 绿 + 5 预存红取得 **master==HEAD 逐字节同构证据**（非本 PR 引入）。

## 数字
- 源码 7 文件（+394/−16 行）+ sidecar/页面 2 数据文件 + 测试 3 文件（+313 行）；11 commits
- RED：master src + HEAD tests 同环境 15 个失败断言/4 个失败用例（三域各一 + R2 域）
- GREEN×2：exprs_ipc 29/176、capability 17/1637、harness 19/295、agent_ops 47/471、parity 12/5506、e2e 4810+4674、r14_C×2、pi 39/39×3
- family ×2：13 二进制 27 运行全绿（mcp_server 9686 断言级等）
- KNOWN_BASELINE_FAIL：CT-6（snapshot registry 腿，04 轨）+ plugin 诊断码族 5 二进制（master 同构证据）
- NOT_EXECUTED：CI(#1491)、Win/mac、EMBED_PYTHON 3 测试（目标不存在于本配置）

## 评审
- R1（fresh 对抗）：READY-WITH-FIXES；2×P1+2×P2+7×P3 全闭（017b0a9f66+71a373cc78）
- R2（第二 fresh，全范围+失败矩阵）：READY-WITH-FIXES；1×P1+1×P2+2×P3 全闭（88a637df6b+c757c2492f）
- 两轮均含五项显式核验（jsoncpp 语义矩阵、别名分析、引用行号、wire 形状、聚合初始化排查）全过

## 工程过程亮点（LOOP_LEDGER 详）
- compile_commands `-fsyntax-only` 预检在占锁前抓出 1 个编译错；HEAD 预览跑抓出 W4 别名缺陷并修复
- 五轨共享全局锁串行（-j2）：R20-01 构建失败（其自身测试命名空间错）现场诊断、排队重排、master 对照周期全部经锁
- 自陷自查：排队命令 git stash 误用风险在执行前发现并纠正

## Remaining debt（移交/后续）
- CT-6 → 04 轨（7 cartography 工具 schema 补齐后 snapshot 三腿全绿）
- plugin 诊断码族 5 二进制预存红 → plugins/sdk 域工单（master 同构证据已归档 /tmp/r20_03_master_*.log 摘要入 TEST_RESULTS）
- D5 mProcessHandle 无锁读（TSAN 级）、D6 种植文件无界读、D8 ClearGuard 无行为差异、D9 CLI 审批面不可达（src/cli 灰区）—— CROSS_TRACK_FINDINGS 记录
- #1628-3 temporal 接线、#1629-1 run/resume 重复、#1630-2 EnumTable：维持 R19-02 DEBT
