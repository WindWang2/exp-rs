# FINAL_REPORT — R25-E（本科遥感实验、LabSpec评分反馈与科学验证闭环深化）

**状态：DONE（含 push+PR；中途外部网络阻断 25+ 分钟后恢复，已诚实记录于 LOOP_LEDGER 第 16 轮）**。BASE `2610cb4741`，分支 `agent/r25-E-teaching-grading-verification-20261028T170411Z`（17 commits），worktree `exp-rs-r25-E-20261028T170411Z`。

## 交付（RED→GREEN×2 + 变异证明 + 双轮独立评审闭环）

| WP | 内容 | Oracle |
|---|---|---|
| WP-E1 | CT-C6+#1695-17c 残留：discovery 协作取消（取消⇒空，非部分）+ preDiscovered 单扫描 seam（digest 外）+ 析构 join 有界化 + **零耐久行运行绝不清空 checkpoint**（R2 P2）+ dock 诚实取消消息 | O-E1 |
| WP-E2 | CT-D1：run_lab.cmd foundry 权威一行修复 + 脚本↔规则↔overlay 三方契约守卫 | O-E2 |
| WP-E3 | #1695-17d：空白作答 record+reload 双边界拒绝，Unicode 空格感知（U+3000 等 17 码位），单一共享 isBlankAnswerText；spec 拒空白 choice（写读不变式） | O-E3 |
| WP-E4 | 取消（#1653 实码复核 FIXED_IN_MASTER @51eb7d1a6，不重修） | — |
| WP-E5 | rubric canonical schema 一致性=既有覆盖（r16_c_t06 绿；#1652-1 已由引擎解析器委托修复） | O-E5 |
| WP-E6 | grading_evidence（行内权威证据上桌，不捏造）+ run_integrity（教师异常透镜）+ loader 成员 round-trip 与敌对值校验（R1 P1/R2 P3-2/3） | O-E6 |
| WP-E7 | 教师全环 E2E + 对抗矩阵 7 类（深 JSON×2/超大/截断/篡改行/敌对计数）+ seam 权威性测试（杀死 useSeam=false 变异） | O-E7 |

## 验收 Oracle 逐项（任务书 §7）
1. ✅ 双场景：学生 foundry（生成→rs:spectral_index→ndvi_landsat PASS=100、digest 双跑一致、错 CRS typed fail、双权威互斥）经 test_r23_d_ct6_lab_flow + 本轨脚本文本/规则钉值守卫；教师全环 E2E 绿
2. ✅ 3 项深化：作答卫生（WP-E3）、评分/科学验证可解释化（WP-E6）、教学管理 UX（WP-E1）
3. ✅ rubric schema：编辑器/引擎/文档/评分结果一致性既有测试绿；规则/工件/版本 hash 变化使旧 checkpoint 失效（#1694 回归绿 + seam 跨路径采用证明）
4. ✅ 跨学生隔离/非法路径/深 JSON/NaN/Inf/缺工件 fail-closed（对抗矩阵 7 类 + 邻域回归）
5. ✅ 教师界面取消可观测、GUI 不阻塞（#1695-17c 主缺陷 r24-c 已修；残留 close-time join 收敛为"一次目录列表+一条目"）
6. ✅ 2 套互斥权威评分规则回归（ndvi_basics fixture vs ndvi_landsat foundry）+ 对抗 ≥4 类；GREEN×2（112/112 ×2）
7. ✅ 零 A/B/C/D 轨所有权文件改动（见下）

## 评审
R1（fresh 子代理）：APPROVE-WITH-FIXES 1P1/3P2/6P3 → 全采纳 → 全量复跑
R2（fresh 子代理，未读 R1 报告）：APPROVE-WITH-FIXES 1P2/4P3 → 全采纳 → 变异证明闭环
详见 REVIEW_R1.md / REVIEW_R2.md；LOOP_LEDGER.md 为逐轮账本。

## 门禁（本地全过）
`git diff --check` exit 0；`check_merge_registration_integrity.py --require-refs` exit 0（1063→1069，lost 0/dup 0/marker 0）；tests/CMakeLists.txt 纯尾部 append（3 个标记块）；.gitignore 仅尾部 allow-list；构建零产物泄漏。

## PR 状态（已验证）
- **PR #1725：https://github.com/WindWang2/exp-rs/pull/1725**
- `gh pr view` 实测：state=OPEN、draft=false、base=master、head=agent/r25-E-teaching-grading-verification-20261028T170411Z@1300e63775abf17133a2d220b36c7af1e08730c2、34 files changed
- push 前复查：origin/master=2610cb4741 未漂移；唯一开放 PR=#1724(r25-D)，交集仅 .gitignore+tests/CMakeLists.txt（双方纯 append，union 解法已记录于 PR_COLLISION_MATRIX）；src/ 零交集
- 不等待远端 CI（协议约定）；不自动合并、不删分支、不清理 worktree

## 网络阻断记录（诚实存档，非当前状态）
- 阻断期现象：`git fetch` → `TLS connect error: error:0A000126:SSL routines::unexpected eof while reading`；`gh` → `Post "https://api.github.com/graphql": EOF`；`getent ahosts github.com` → 198.18.0.72（RFC2544 保留段伪造 IP）；SSH 22 端口连到同一伪造 IP 后立即关闭。持续 25+ 分钟重试未恢复，期间已在上一轮回复中如实报告"PR 未创建"。
- 恢复后动作：fetch 复核 → 冲突分析 → push → gh pr create → 验证 OPEN/非 draft。**从未在阻断期谎称已提交 PR。**
