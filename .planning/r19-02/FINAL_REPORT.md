# R19-02 FINAL_REPORT — Pi/MCP Harness、工具契约、科学决策与 Workflow 闭环可靠性

## 分支与基线
- Branch: `agent/r19-02-pi-mcp-workflow-20261020T102409Z`（worktree 独立；PR 待创建时填充编号）
- BASE `e12a9afbc` = 推送前最后一次 LIVE 核对的 origin/master（gh api 复核）；rebase 无需
- 10 commits：1 检测器 + 1 恢复 + 3 行为修复 + 1 快照权威 + 2 评审修复轮 + 2 文档

## 头条成果
1. **发现并修复一起未被任何人察觉的合并事故**：merge 5e14bd1d7b（2026-10-18，r16-c 轨）静默回滚了 #1518 已合并的 #1505-review-P1 修复（science_context capabilityFacts 生命周期所有权），连同 253 行回归测试一并丢失，当前 master 带病运行两天。以新检测器发现、双 RED 实证、verbatim 恢复、三套 GREEN×2 闭环。
2. **沉淀 merge-loss 检测器**（B13）：29596c913（已知事故）精确命中校准 + 5e14bd1d7b（新事故）发现；全史 540 merges 224 findings 分诊出 5 组跨轨线索（CT-1..CT-5）。
3. **关闭 #1654 残余取消竞态窗**：requestCancel store 与 runStep consume 经 m_mutex 全序化；4 用例 1009 断言行为网。
4. **修复 master 上的 surface 快照漂移**（B03）：#1662 sweep 手改快照 vs r12-a 代码（mosaic 文案）；再生暴露并记录 r18-C 规范化投影与 registry 腿的契约互斥（workspace 工具本轨修复，cartography 7 工具跨轨 CT-6）。
5. **恢复 pi knowledge 预算门**（39/39）；清理 #1636 死成员。

## 数字
- 修复/恢复：行为修复 4 项（T0/T1/T6/T7）+ 死成员 1 项（T2）+ 工具 1 项（T3）；评审 P2/P3 修复 5 项
- 文件：46（src 9、tests 5+快照、sidecars 16、scripts 3、pi 1、planning 12）
- 测试：本轨族 14 套 ×2（约 4.6 万断言/轮）、pi 39×2、unittest 7×2；T0/T6/T7 真实 RED 复现
- 已知失败：1（CT-6 红区，确定性，跨轨移交）；NOT_EXECUTED：CI(#1491)/Win/mac、plugin_host 全量（后续轮）

## 评审
- R1（fresh）：READY 无 P0/P1；P2×1 记录（T1 行为网性质）、P3×3 当日修复
- R2（第二 fresh，全范围+跨轨）：READY 无 P0/P1；P2×1（CT-6 集合错误）当日修复；P3×2（R12 记录/清理）

## Remaining debt（后续轮/跨轨）
- CT-6（轨 03，7 工具两类 schema 缺项）→ 修后 surface_snapshot 全绿
- CT-1..5 跨轨线索；R12 ClearGuard 竞态；#1628-3 temporal 工具接线（D-160-1）；plugin_host 进程族全量回归；B11 性能记账项
