# R19-02 Loop Ledger — Pi/MCP Harness、工具契约、科学决策与 Workflow 闭环可靠性

Goal-loop 账本（单一事实源；任务书指定路径）。每轮：`第N轮 | 改动 | 验证结果 | 通过/未通过 | 下一步`。

## 会话元数据
- Track: R19-02（Pi/MCP + Workflow harness reliability）
- Worktree: `/home/kevin/project/exp-rs-02-pi-mcp-workflow-20261020T102409Z`
- Branch: `agent/r19-02-pi-mcp-workflow-20261020T102409Z`
- BASE_SHA: `e12a9afbc8883a474dc9ffcaf3345ae55863dc71`（2026-10-19 LIVE 验证 = GitHub master）
- Oracle: 任务书 O-B1..O-B6（ORACLES.md 逐条落列）
- 全局构建锁: `/home/kevin/project/exp-rs/.git/r19-global-j2-build`，全局 -j2，ctest -j1

## 环境事实
- git fetch HTTPS 通道 SSL 中断（OpenSSL unexpected eof），但本地 origin/master 经 `gh api` 核对与远端一致（e12a9afbc）。后续远端核对用 `gh api`，git fetch 定期重试。
- `ninja` 用户别名带 `-j40` —— 一切构建必须经 resource_guard.py 显式 `--parallel 2`。
- GDAL 来自外部 prefix `/home/kevin/project/paleo-singlefactor-closure/vendor/prefix/usr`（沿用 r18-c 验证过的解析方式，非本项目内构建）。
- 开放 PR：#1663（r18-a 科学算法）、#1664（r18-e 教学/核验）→ 红区只读。
- 未合并远端分支仅上述两个 PR 的分支（r18-a ahead 145 / r18-e ahead 12），其余全被 master 包含。

## 轮次记录

### 第1轮 | LIVE recon + worktree + 文档骨架 | 通过
- recon：master=e12a9afbc（gh api 核对）；开放 PR #1663/#1664；P1×4 全部已修（#1662 sweep），P2×11 全部已修，P3×22 中已修 19、剩 #1628-3(声明后续)/#1628-4(死分支)/#1636(死成员)/#1629-1/#1629-3/#1630-2(纯重构债)。
- r18-c 残留核验：rpc-id 跨代重用=非问题（nextId 单调）；RateLimiter 反向抑制=无生产者不改；get_tool_schema 投影=无实测瓶颈记账。
- configure：3 次失败（GDAL→PROJ→GSL）后以 r18-c 同款依赖提示集成功；全量构建后台启动。
- 产出：OWNERSHIP.md / TASKS.md / RISK_REGISTER.md / configure.sh。

### 第2轮 | T3 merge-loss 审计器 + ★T0 核心发现★ | 通过（T3 已提交）
- T3 `scripts/dev/merge_loss_audit.py` + 7 项 hermetic 测试 GREEN×2；真实历史校准：540 merges→224 findings，**29596c913 命中其已知 30 路径集**（与 r18-c 取证一致）。
- ★新事故发现★：**merge 5e14bd1d7b**（r16-c 轨 2026-10-18 "merge master and keep both track ignore rules"）静默丢弃 **#1518 (a3465fc53) 的 #1505-review-P1 修复**：src/science_context/{broker.h,broker.cpp,agent_adapter.cpp} + 3 个测试文件。当前 master 仍缺：`capabilityFacts()` 交出 `&*mCapFacts`（optional 存储裸指针），并发 `setCapabilityFacts()` move-assign 后调用方读错 authority/悬空。丢失后这些文件零提交 → 恢复无碰撞。
- 分诊其余 findings：4c906605/2a86f65=r18-a 红区(#1663 处理中，检测器独立交叉验证)；c8f5e080=app/teaching 14 路径（轨 03 线索）；68ac5398=benchmarks/observatory 数据文件+1 知识文档；899eb5718=ADR 0146 文档；2a0147c=test_classification_e2e 单测试。→ CROSS_TRACK_FINDINGS.md。
- 下一步：构建完成后 T0 恢复（测试先入→RED 行为复现×1→src 恢复→GREEN×2）。

### 第3轮 | T0-T6 准备 + 构建环境修复 + pi 基线 | 进行中
- 全量构建失败于 qgis_core 链接：vendor prefix 非 PIC `libsqlite3.a` 被选中 → configure.sh 钉 SQLite3/ZSTD/PCRE2_8 到系统 .so。
- pi Node 基线：39 测试 38 过 1 败 —— `capability-temporal.md` 33384B > 32768B 预算（56931bf68 的 sidecar 文案加富所致）= 真实 RED（T6）。
- T0 RED 前置：#1518 测试文件已恢复到工作树（253 行纯增量）；src 未动。
- T1 已编码（未提交）：requestCancel store(true) 与 runStep consume 均入 m_mutex（新增 cancelFlagLocked）；新测试 test_r19_02_workflow_cancel_order.cpp（消费语义/执行中兑现/200 轮并发栅栏/负向）+ tests/CMakeLists.txt append-only 注册。
- T2 已编码：explain validator 死成员 guidance_ 与构造参数移除，唯一调用方更新。
- T6 sidecar 文案精简两轮（-947B decoded，24 行外科手术式 diff；首轮 json.dumps 全文重排已回滚重做为转义原位替换）；待 gen-pages 重生成 + 双门验证。
- BASELINE.md / ORACLES.md / CROSS_TRACK_FINDINGS.md / RISK_REGISTER.md 完成。
- 下一步：锁释放→构建→T0 RED 确认→src 恢复→GREEN×2→依次提交 T0/T1/T2/T6。

### 第4轮 | 构建环境攻坚（6 次失败定位与修复） | 通过
- 失败链：vendor 非 PIC libsqlite3.a → 隐式 -L 使 blas/lapack 混解析（ztrsm_64_）→ pcre2_16@10.47 版本符号 → vendor gdal wrapper2_*（前缀内零提供者，采 r18-c 同款 --allow-shlib-undefined）→ 运行时 libarmadillo 被外部裁剪（共享前缀漂移；SONAME 桩补齐）→ vendor 旧 Qt6Core 抢占（系统 Qt 全套 symlink 进 shim）。
- 流程事故与修复：configure.sh 内嵌同锁 resource_guard 在外层持锁时自死锁 → 改为纯 cmake、调用方持锁；被杀锁用 --stale-after 120 快速回收。
- 产出：configure.sh 全决策记录 + link-shim 方案。

### 第5轮 | T0-T2-T6 RED→GREEN→提交 + R1 评审 | 通过
- T0：RED×2 实测（revision 9==7；churn 1==600）→ src 恢复 → GREEN×2（三套全过；live_authorities 前 2 败=PROJ_DATA 环境）→ a29f935e0。
- T1：GREEN×2（1009+74 断言）→ dce4cb180。T2：GREEN×2 → 75ba7ad94。T6：gen-pages 重生成 32521B、双门绿 → 7944fa75a。
- R1 独立评审：READY 无 P0/P1；P2（T1 测试=行为网）记录；P3×3 修复于 14057e801。详见 REVIEW_R1.md。

### 第6轮 | T7 surface 快照权威 + 全族回归 | 通过（含 1 项红区 KNOWN_BASELINE_FAIL）
- 发现 master 快照与 master 代码漂移：#1662 sweep 手改的快照仍含 r12-a 已删的 mosaic "(band 1 used)"（两腿 RED 实测）。
- 再生暴露深层契约互斥：r18-C 规范化投影 vs registry 腿钉 raw —— property-less schema 两腿互斥。workspace:undo/redo（本轨区）修为 canonical（3ff4e0ba4）；cartography 5 工具=轨 03 红区 → CT-6 记录，快照保持 wire 权威。
- 全族 14 套 ×2：11 全绿；parity/e2e 在补建 CLI/host 二进制后全绿；snapshot 仅 registry 腿红区 1 败（确定性，失败信息自诊断）。
- pi 全套 39/39 ×2；scripts unittest OK ×2。
- 下一步：R2 评审 → fetch 复核/rebase → push → PR → FINAL_REPORT。

### 第7轮 | R2 评审 + P2 修正 + PR 就绪 | 通过
- R2（第二 fresh 子代理，全范围）：READY 无 P0/P1；P2×1=CT-6 失败集枚举错误（5→7 工具、两类缺项、lint_catalog 本就 canonical）→ CROSS_TRACK/PR_BODY/ORACLES/TEST_RESULTS 四处修正 + REVIEW_R2.md 存档（5b5d8b733）；R2 P3×2（ClearGuard 竞态→RISK R12；.tif 残留→已删）。
- master 复核未动（e12a9afbc）；与 #1663/#1665 零重叠、与 #1664 仅 tests/CMakeLists.txt 双方 append-only（merge-tree 0 冲突）；diff --check clean；46 文件全在区内。
- 终验矩阵见 TEST_RESULTS（14 套族 ×2 + pi 39/39 ×2 + unittest ×2；唯一红=surface_snapshot registry 腿 CT-6 红区确定性失败，已跨轨记录）。
- 动作：push 分支 → gh pr create → FINAL_REPORT。
