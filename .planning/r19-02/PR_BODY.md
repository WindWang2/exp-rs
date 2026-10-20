# hardening(r19-02): Pi MCP harness, workflow and plugin contract closure

**BASE_SHA** `e12a9afbc8883a474dc9ffcaf3345ae55863dc71` · **HEAD** `ba503cee1`（code head 5b5d8b733 + docs）

## 职责边界
R19-02 轨（Pi/MCP Agent Harness、工具契约、科学决策与 Workflow 闭环可靠性）。全部改动在本轨 OWNERSHIP 区（`.planning/r19-02/OWNERSHIP.md`）；`tests/CMakeLists.txt` 纯 append-only；与开放 PR #1663/#1664/#1665 零文件冲突（merge-tree 验证）。

## 问题清单与证据（每项 RED→最小修复→GREEN×2）

### 1. ★新发现事故：merge 5e14bd1d7b 静默回滚 #1518 的 #1505-review-P1 修复（a29f935e0）
r16-c 轨 2026-10-18 的 gitignore 合并把 science_context 6 个文件整体解到旧侧，`a3465fc53`（capabilityFacts 裸指针→shared_ptr 快照）连同 253 行测试消失至今。当前 master 上并发 `setCapabilityFacts()` move-assign 后，跨锁持有 `capabilityFacts()` 视图的调用方读到**新** authority 或悬空。恢复自 #1518 头（丢失后零触碰，无碰撞）。
- RED×2 实测：broker `snapshot->revision() == 9 where 7 expected`；concurrency churn `1 == 600` coherent
- GREEN×2：broker 40例/183断言、concurrency 14例/10130、live_authorities 15例/197

### 2. merge-loss 检测器 `scripts/dev/merge_loss_audit.py`（c79c5c60d + R1 修复 14057e801）
B13 工单：post-merge 不变量检查——一侧独占变更在合并结果中等于 base 态即报丢失（M/A/D 一条件覆盖），双侧冲突不误报。真实史校准：540 merges → 29596c913 精确命中已知 30 路径集，并由此发现上条事故。7 项 hermetic 测试 GREEN×2。

### 3. workflow 取消窗口全序（#1654 残余，dce4cb180）
`requestCancel` 的 `store(true)` 与 runStep 入口的陈旧取消消费 `store(false)` 均在 `m_mutex` 外、无全序：交错 A 丢取消（flag 被 consume 且无 task 可 cancel）、交错 B 跨步误伤。两处移入同一锁（新增 `cancelFlagLocked`；TaskCenter 调用留锁外）。新测试 4 用例（消费语义/执行中兑现/200 轮并发栅栏+后续步不中毒/未知会话无操作）1009 断言 GREEN×2；回归 test_workflow_cancel 74 断言 GREEN×2。全序性质检视证明+行为网（头注释声明）。

### 4. explain 死成员 `guidance_`（#1636，75ba7ad94）
构造存入从不读取（含 #1505 时代自查注释"stored for future use"）。删除成员/参数/两处调用点参数。三套 explain 21+218+70 断言 GREEN×2。

### 5. pi knowledge 预算超限（7944fa75a）
R10-A sidecar 文案加富使 capability-temporal.md 33384B > 32KiB 预算门（pi 39 中 1 败，实测 RED）。16 个 rs-temporal sidecar 精简 −947B（转义原位替换、无 JSON 结构变化；删除子句均 condensed 保留或指向摘要行）；gen-pages 重生成 32521B，`--check` zero diff，pi 39/39 ×2。

### 6. surface 快照权威（B03，3ff4e0ba4 + R2 修正轮）
#1662 sweep 手改的快照与 master 代码漂移：rs:mosaic 描述仍含 r12-a（8fe92f959）已删的 "(band 1 used)"（两腿 RED 实测）。按文档化 `surface_snapshot_regen` 流程再生 371 工具（名称集与顺序逐一致，无增删）；再生暴露更深契约互斥——r18-C 规范化投影（wire 权威）对缺项 schema 补 `type:"object"`/`properties:{}` vs registry 腿钉 raw：`workspace:undo/redo`（本轨区）修为 canonical 形态；cartography 7 工具属轨 03 红区（4 缺 properties、3 缺 type；lint_catalog 已 canonical）→ CT-6 跨轨记录（R2 核正），快照保持 wire 权威（projection+wire 腿全绿；registry 腿红区确定性 1 败=KNOWN_BASELINE_FAIL，REQUIRE 首个不匹配中止，失败信息自诊断）。

## 测试与验证
- 全部本地（CI 全仓失效 #1491）；环境/锁/exit 码全录 `.planning/r19-02/TEST_RESULTS.md`
- 本轨族 14 套 ×2 全绿（除上述红区 1 例）；pi 39/39 ×2；scripts/dev unittest ×2
- T0 双 RED / T6 RED / T7 RED 均为真实失败复现，非推断

## 独立评审
- R1（fresh 子代理，只读，独立复算检测器数学与 blob 考古）：**READY 无 P0/P1**；P2=1（T1 行为网性质，已声明）、P3×3 已修
- R2（第二 fresh 子代理，重点 T7/终形态/跨轨）：**READY 无 P0/P1**（见 `.planning/r19-02/REVIEW_R2.md`）

## 跨轨
- CT-1/2/3/4/5（merge_loss_audit 全史审计线索：app/teaching 14 路径、r18-a 6 路径交叉验证、benchmarks、ADR0146、test_classification_e2e）
- **CT-6**：cartography 7 工具 inputSchema 需补规范化缺项（4 个补 `properties:{}`、3 个补 `type:"object"`；lint_catalog 已 canonical）——轨 03 一行式修复，解除 KNOWN_BASELINE_FAIL

## 已知风险 / KNOWN_BASELINE_FAIL / NOT_EXECUTED
- KNOWN_BASELINE_FAIL：test_surface_snapshot registry 腿 × cartography 7 工具（CT-6，红区；REQUIRE 首个不匹配中止故显示为 1 败）
- NOT_EXECUTED：CI（#1491）、Windows/macOS lane、plugin_host 进程族全量（后续轮）
- 性能：本 PR 无性能主张（B11 仅记账：get_tool_schema 单工具投影无实测瓶颈、RateLimiter 反向抑制无生产者）
