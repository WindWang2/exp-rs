# BASELINE — R25-E

- 查询时间：2026-10-29 (UTC)，gh CLI 实查
- origin/master = 2610cb4741e69a89124921e44c03e340e2522ee2（2026-10-27 16:49 UTC merge #1723）
- Open PR = 0；Open Issues = 144
- 远程遗留 r23*/r24* agent 分支（fetch --prune 后仍在）：r23-a/b/c、r24_a/b/c（均 ahead_by=0，无未合并内容）
- 本地兄弟 worktree（同主机五轨并行）：r25-A(140339Z)/r25-B(140313Z)/r25-C(140339Z)/r25-D(170355Z)，均未 push
- 本轨 worktree：exp-rs-r25-E-20261028T170411Z，分支 agent/r25-E-teaching-grading-verification-20261028T170411Z
- 主仓 master 与 origin/master 同步；仅未跟踪 .workbuddy/（不属本轨，不动）

## 前置 PR（已读）
- #1711 (r22-teaching)：#1694 checkpoint 权威身份化、#1689 cohort 泄露门、#1678 NaN/Inf、#1688 cmd 注入
- #1716 (r23-E)：插件隔离/交付回归（生产 src 零改）
- #1717 (r23-D)：ndvi_landsat foundry 权威 + session 深度炸弹 + ±inf 门 + 回归矩阵
- #1721 (r23-C)：#1695-3 GUI 发现扫描下放 worker、#1695-5 双工件原子发布、%n 复数；CT-C6 移交本轨
- #1722 (r23-A)、#1723 (r23-B)：科学算法/数据模型（无教学交集）

## 与本轨相关的 LIVE 缺陷（W0 实码核验）
1. **CT-D1（LIVE）** scripts/windows/run_lab.cmd:37 `--lab ndvi_basics` 评 foundry 产物 → master 必然 45/100。r23-D CROSS_TRACK 明确建议 E 轨一行修复 `--lab ndvi_landsat`。已核 overlay 参数逐位一致。
2. **CT-C6（LIVE）** src/teaching_admin/batch_assessment.{h,cpp}：discoverSubmissions 无取消钩子；runBatchAssessment 内部重发现（GUI 批处理每班扫描两次）；~TeachingAdminDock 析构 join 一个不可取消的目录扫描（close-time 阻塞窗口）。
3. **#1653（LIVE，P3）** src/verification 三个 TU 私有复制 beginResult，违背自身导出理由。
4. 候选核验：#1695-17a AsyncGdalRunner 死代码（未核）、#1695-17d recordAnswer 空白答案（未核，#1678 已修 NaN/Inf）。

## FIXED_IN_MASTER_BUT_OPEN（严禁重修，可加回归）
- #1694（checkpoint digest）、#1689（泄露门）、#1678（NaN/Inf）、#1688（cmd 注入）、#1652-1（rubric schema 漂移→引擎解析器委托）、#1652-2（feedback 前缀误报）、#1695-3（GUI 发现阻塞）、#1695-5（双工件发布）、#1695-i18n 复数
