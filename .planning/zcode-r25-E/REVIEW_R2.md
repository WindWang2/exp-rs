# REVIEW_R2 — R25-E（独立评审人 2，全新上下文子代理，未读 R1 报告）

- 结论：**APPROVE-WITH-FIXES**（1×P2 阻断、4×P3）
- 验证方式：全量 diff + 周边生产码 + 六套新测试 + ctest 直跑（r25_e 21/21；扩展回归 43/43；TEST_RESULTS 声明套件 29/29）+ 两仓库门禁
- R1 修复逐项复核确认有效（round-trip 成员、blank-choice + 单一 isBlankAnswerText、去空洞取消、原子计数、守卫种植位置、空类对照）

## 发现与处置（全部采纳，commit 201db42c6e + test commit）

| # | 级别 | 发现 | 处置 |
|---|---|---|---|
| 1 | P2 | checkpoint 守卫键错条件（items.isEmpty() 而非"零耐久行"）：两扇门仍会清空既有 checkpoint——(a) 提交目录瞬时缺失（无取消标志，cancelledEarly=false，守卫不触发）；(b) dock 指纹流期间取消（seam 非空、全部行取消、零填充槽位）；且控制台随后谎称 "durable partial results published" | 守卫改为 `durableRows > 0`（filled 槽位计数）：任何产出零耐久行的运行绝不写 checkpoint；dock 取消消息按 rows 空否分流为 "nothing graded — previous outputs and checkpoint left untouched"；新增测试（dock 真实形状：seam+预置取消+digest 分歧 + **字节级** checkpoint 保全断言）+ 变异证明（恢复旧行为→2 测试红） |
| 2 | P3 | 新 loader 成员无校验：负整数 truncated/resumed 原样进 run_integrity（不可能负数） | readCount 校验（负/非整数→0+warning，与 total 同纪律）；A7 对抗测试 |
| 3 | P3 | skips 循环静默丢弃坏条目 vs rows 循环 typed 拒绝（#1466 记账面被低报） | 非对象 skipped_files 条目=typed 拒绝；非数组成员=typed 拒绝；A7 测试 |
| 4 | P3 | seam 无纵深防御（caller 完全拥有"哪些文件是提交"的权威） | 记录为契约（唯一 feeder=dock 自身同目录同函数遍历；头文件已文档化 freshness/ordering 归属）；新增 seam **权威性**测试（subset 列表→只评 1 人，杀死 useSeam=false 变异）+ R2 F 变异分析入账 |
| 5 | P3 | tests/CMakeLists.txt 尾部两块共用一个 end 标记 | 记录（merge gate 已验证纯 append、0 重复、0 标记问题）；不改动他人块排序 |

## R2 的变异逃逸分析（F part 2）→ 已闭环
`useSeam=false` 变异（静默退回双扫描）原可存活——所有测试都断言"与内部发现等价"（同目录时两者观测相同）。新增 seam 权威性测试后该变异必红（subset 场景下 total 从 2 变 1）。

## Oracle 勾稽修正（R2 G 节）
- O-E4（#1653 beginResult）：WP-E4 已取消（FIXED_IN_MASTER），oracle 从清单移除
- O-E2 后半（其他 repo 脚本不指向 fixture 评 foundry）：人工核验为真（grade_all.cmd 参数化默认；offline_smoke.sh 用 bundle overlay），无测试、记录在案
- O-E7 学生场景：经 r23-D 既有 test_r23_d_ct6_lab_flow 锁定（本 PR 测试锁定脚本文本+规则钉值），依赖关系已文档化

## 独立评审真实性
R1/R2 均为独立 fresh-context 子代理（不同 agentId、互不读报告）；R2 明确声明未读 R1 报告并独立复跑了全部测试与门禁。
