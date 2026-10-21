# R18-E TASKS — 工作包分解（每项: Recon→RED→定位→极小修复→GREEN×2→对照→Review）

## WP-0 基线（必做）
- [x] LIVE recon + 三路深读审计
- [x] 治理文档
- [ ] build-r18-e 配置 + 本域目标编译 + 本域测试基线 GREEN（记录进 TEST_RESULTS.md）

## WP-E01 教学状态真值（E01 阶段）
- [ ] T101 D6: `lab_status` 恢复权威 — restored lab_status 与 last_validation_summary 一致性校验（RED: 手改 completed + 空摘要 → 现被原样接受）
- [ ] T102 D7: 伪造整体 pass 摘要 — `last_validation_summary` 增加 verdict_artifact_digest 绑定；artifact 变更/digest 缺失 → 恢复为 Indeterminate（RED: 手写 pass 文档现被渲染绿）
- [ ] T103 D1(契约部分): src/teaching 完成权威记录 API（LabCompletionLedger: 记录 labId+verdict+artifact digest+时间无关序），dock 接线入 CROSS_TRACK_FINDINGS
- [ ] T104 V2: runSpec 拒绝重复 check id + rollUp 防御合并（RED: [x(Pass),x(Fail)] 现汇总为 Pass）

## WP-E02 恢复与攻击性输入
- [ ] T201 D8: lab session seq 总账 — last_seq 抬升/重复 seq 检测（RED: 伪造 seq gate pass 现被 latest-wins 采纳）
- [ ] T202 H3: StudioSessionState::fromJson 校验 schema（RED: /2 文档现被接受）
- [ ] T203 D7 回归: #1519/#1525 已修 case 只做回归 pin

## WP-E03 教师批量评阅
- [ ] T301 D2: CLI 批量发现 symlink/悬空链接拒绝 + skip 行（RED: ln -s /etc/passwd 现被评分）
- [ ] T302 D4: stamp TOCTOU — 评分前取 pre-stamp，评分后 post-stamp 不一致 → 行降级 Indeterminate 需重评（RED: 现旧分绑新字节）
- [ ] T303 D3: checkpoint 行判定真实性 — verdict 从行内 transcript 摘要重导出校验（RED: 手插 pass 行现被采用）
- [ ] T304 D5: rubric_builder 对齐 grader 权威（weight∈(0,100], Σ=100±1e-9, passing_score∈[0,100], severity 词表）（RED: [50,50,0] 现过 preflight）
- [ ] T305 D9a: `lab --grade --out` 原子写; D9d: GUI batch duplicate_of 旗标（与 CLI 对齐）

## WP-E04 证据规则覆盖
- [ ] T401 V4: scientificContractFor/builtin packs 词表对齐求值器（RED: 现全 SPEC_INVALID）
- [ ] T402 V7: relational 无 name operand 拒绝或强制具名（RED: 自比恒 Pass）
- [ ] T403 V9: specDigest 比较器纳入 params（RED: 并列不同 params 组合序不同 digest）
- [ ] T404 V8: stage 无 stage 块时拒绝而非默认 Completed（RED: 现静默默认）
- [ ] T405 回归: 未知 kind→Indeterminate 不回退 skip 的既有 pin

## WP-E05 studio 投影
- [ ] T501 H6: spatialSummaries 按 selectedPointId 关联（RED: 现取 first()）
- [ ] T502 H7: uncertainty_missing_replicate 误报（band 存在即证明有 replicate）

## WP-E06 FaultLab
- [ ] T601 V3: Equals/DeltaGe/DeltaLe 缺 clean 基线 → not-evaluated（RED: 现 delta=0.0 空洞通过）
- [ ] T602 V6: expected.verifier 未执行 → 报告标记 unevaluated（不再静默丢弃）
- [ ] T603 V10: grid_shift(0,0) 拒绝; 诊断规则缺基线不伪造签名

## WP-E07 Help/i18n
- [ ] T701 H1: severity 词表 fail-closed（未知值→数据错误，不静默 Error）（RED: "Warning" 现静默变 Error）
- [ ] T702 H4: toJsonText 预算不足 → 合法 JSON 最小文档（RED: 现输出截断非法 JSON）
- [ ] T703 H5: elide()/toJsonText 代理对安全（复用 #1526 helper）
- [ ] T704 markdown 注入 contract test（help_markdown_writer 转义面）

## WP-E08 评分与权限边界
- [ ] T801 权限拒绝/隔离路径回归矩阵（student_projection 泄漏 oracle 已有 → 扩充 r18_e）
- [ ] T802 D5 生效后 preflight↔batch 奇偶测试（preflight 拒绝的 rubric 批量必拒）

## WP-E09 性能
- [ ] T901 ErrorDiagnosticsBridge 每查询全表重建 → 缓存（语义不变）
- [ ] T902 基准: help search 5k、批量 1000 行、verifier 大 spec（记录 PERFORMANCE.md）

## WP-E10 黄金闭环
- [ ] T1001 端到端: 实验包可用性→提交→验证证据→批阅→反馈→中断恢复→复核（一个 test_r18_e_golden_loop）
- [ ] T1002 V5: derivedStatus 与 report.status 一致性（render_teaching 用 rollUp 同源）

## WP-R 评审与交付
- [ ] R1 独立逆向评审 → fix → R2 再审（REVIEW_R1/R2.md）
- [ ] review_pack.py、PR 正文、push、gh pr create
