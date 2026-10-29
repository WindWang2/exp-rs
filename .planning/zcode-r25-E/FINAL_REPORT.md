# FINAL_REPORT — R25-E（本科遥感实验、LabSpec评分反馈与科学验证闭环深化）

**状态：DONE（除 push/PR——外部网络阻断，证据见下）**。BASE `2610cb4741`，分支 `agent/r25-E-teaching-grading-verification-20261028T170411Z`（17 commits），worktree `exp-rs-r25-E-20261028T170411Z`。

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

## 阻断项：push/PR 未能执行（外部网络）
- 精确错误：`git fetch origin --prune` → `fatal: unable to access 'https://github.com/WindWang2/exp-rs.git/': TLS connect error: error:0A000126:SSL routines::unexpected eof while reading`；`gh pr list` → `Post "https://api.github.com/graphql": EOF`
- 根因证据：`getent ahosts github.com` → `198.18.0.72`（RFC2544 保留段伪造 IP；IPv6 `2001:2::48`）；`ssh -T git@github.com` → `Connection closed by 198.18.0.72 port 22`；TCP 443 可连但 TLS 立即 EOF。会话早期 gh CLI 曾可用（144 open issues 实查），此后沙箱对 GitHub 的 DNS/连接被拦截，持续 25+ 分钟重试未恢复。
- **本地可恢复**（worktree 就绪，17 commits 干净）：
  ```bash
  cd /home/kevin/projects/rs-studio/exp-rs-r25-E-20261028T170411Z
  git fetch origin --prune                       # 确认 master 未漂移（断线前=2610cb4741）
  git push -u origin agent/r25-E-teaching-grading-verification-20261028T170411Z
  gh pr create --repo WindWang2/exp-rs --base master \
    --head agent/r25-E-teaching-grading-verification-20261028T170411Z \
    --title "r25-E: 本科遥感实验、LabSpec评分反馈与科学验证闭环深化" \
    --body-file .planning/zcode-r25-E/PR_BODY.md
  ```
  PR_BODY.md 已含 BASE/HEAD SHA、分支、Issue 归属、每个 WP 的 RED/GREEN×2、命令与退出码、R1/R2、跨轨风险、未执行平台。**未谎称已提交 PR。**
