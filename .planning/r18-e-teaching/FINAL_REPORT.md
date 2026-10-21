# R18-E FINAL REPORT — 教学实验、科学证据与可复现评价：状态真值与完整反馈闭环

- 基线: 任务书快照 7825c1ebd → LIVE 起点 a60c8831c → **交付基线 e12a9afbc**（rebase 对齐竞争修复后）
- Worktree: /home/kevin/project/exp-rs-r18-e-teaching, branch `agent/r18-e-teaching-evidence`
- Commits: 12（含 2 轮评审修复 + 跨轨 pin 对齐 + review pack）；`git diff --check` = 0

## 交付内容（按工作包）

| 包 | 修复 | 证据 |
|----|------|------|
| E01 状态真值 | verdict stamp（摘要+工件字节绑定，save 权威盖章/load 复核，篡改→Indeterminate）；lab_status 降为摘要投影；零行 pass 拒绝；LabCompletionLedger 完成权威（仅盖章 pass 可入账，load 复核证据链，投影 progress 文档）| T101/T102a-f/T102*/T103* + golden loop |
| E02 恢复与攻击输入 | seq 账本密度不变量（恰为 1..last_seq：拒膨胀/空洞/重复/前缀截断）；StudioSession schema 门 | T201a-f/T202 |
| E03 教师批量 | stamp TOCTOU 双向（中途换件/出现→unavailable+空章重启重评）；adopted 行进程内 resumed 标志+报告级计数（发布字节同一性保持）；CLI 发现 symlink/悬空/特殊项可见拒绝（refused_entries）；rubric 校验对齐评分权威（(0,100]/Σ=100/severity/passing_score）| T301/T302/T302b/T303/T304* |
| E04 证据规则 | 重复 check id：runner 拒绝+rollUp worst-wins 防御；relational 自比 operand 拒绝；specDigest 并列 tiebreak 纳入 params | V2/V7/V9 + 既有 r16/r14 回归 |
| E05 studio | 空间 delta 归属 selected point；uncertainty_missing_replicate 只标记缺失证据、不再误导重声明 | T501/T502 |
| E06 grader/faultlab | AtMost 线性窗与 AtLeast 精确镜像（死代码复活）；stage 无块拒绝（默认 Completed 移除）；faultlab 数值关系无基线/无操作数 fail-closed；text 操作数豁免（text observable 合法、number 上 fail-closed）；grid_shift(0,0) 拒绝；诊断规则需 present 基线 | V1/V3/V3c/V3d/V10/V10b |
| E07 help | severity 封闭词表 fail-closed（present-but-invalid 拒绝）| T701/T701b |
| E08 边界 | preflight↔batch 奇偶（rubric 对齐）+ 权限/隔离回归绿 | T304 系列 + 既有泄漏 oracle |
| E09 性能 | search gate ×3 实测（15.8–16.4s 全 PASS）；bridge 缓存对齐 master 方案（取 master，见竞争处置）| PERFORMANCE.md |
| E10 黄金闭环 | 工件→盖章 pass→重启保持→入账→再提交降级→再验证复得，全程状态真值断言 | test_r18_e_golden_loop |

## 与 master（e12a9afbc）竞争修复的处置

rebase 发现 master（#1662 全量评审清扫 + r18-b/c/d 合并）已修 V4（builtin/derived pack 词表对齐——比本轨方案更积极可判定）、V5（未认领结果折入）、V6（loader 直接拒绝非空 verifier——更严）、H4/H5（presenter 降级链+truncateSummary 复用）、E09（bridge 带 registry 身份的 memo）。**全部取 master 版本，本轨对应实现与测试 pin 对齐其契约**（G pin 跨轨修正为 CT-5）。本轨独有且保留：verdict stamp/完成账本、seq 密度、TOCTOU 双向、rubric 奇偶、CLI 发现加固、重复 id、自比 operands、specDigest tiebreak、AtMost 镜像、stage 拒绝默认、faultlab 基线/操作数诚实、诊断基线门、severity 词表、CSV 前导空白（含 padded 负数保型）。

## Oracle 达成

- A 重复性: 26 RED（修复前实测）→ 11 目标 ×2 连跑 GREEN；R1 3×P1 补 RED（T201e/T201f/T304f/T302b/V10b）
- B 本地验证: 全部本域可构建套件 PASS（grader 320/faultlab 564/lab_runtime 698/admin_core 3450/verifier r16a 153/packs 299/…）
- C 性能: 实测入 PERFORMANCE.md；未测比例不声称（bridge 微基准 NOT_EXECUTED 如实标注）
- D 回归契约: 16+ 既有 pin 套件全绿；fail-closed 语义无回退
- E 所有权: diff 逐文件对照 OWNERSHIP（例外: tests/CMakeLists.txt 追加注册块、test_r16_a_verifier.cpp G pin 跨轨修正、data/faultlab schema 同步、.gitignore 锁文件——均在 PR 标出）
- F Review: R1 NOT-READY（3×P1+7×P2 全处置）→ R2 READY（1×P2-NEW+4×P3 全当场修复双跑）→ 见 REVIEW_R1/R2.md
- G 交付: 原子 commits、无 secrets/构建产物（锁文件误入已修正+gitignore）、diff --check 0、review_pack 完整

## NOT_EXECUTED / 开放事项

1. `test_r18_e_batch_cli`（链 sicnu_agent→qgis_core 重链）——源码注册已交付、R2 代码级评审通过；构建在五轨锁竞争中后台进行，交付时未完成即如实标注（D2 行为由 lab_batch_runner 单元语义与代码评审背书）
2. sanitizer/race lane 未跑（五轨共享宿主下不可行）——静态互斥纪律经 R1 并发审查
3. keyed-MAC 判定绑定（DECISIONS 后续工作）；CT-1/2/3 跨轨接线建议
