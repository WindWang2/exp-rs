# R22-03 TASKS（里程碑视图；每包=一个 15 轮 /goal-loop 循环）

## C0 [#1706] 多类别羽化拼接结果损坏 — P1（本 PR 主工作包）
- [x] 动态验真（源码逐行 + issue 独立复现交叉核对）→ OPEN_UNFIXED
- [x] RED：`tests/test_r22_inference_feather.cpp` 5 个 TEST_CASE
      （probability 三类独立 + seam 余弦已知解；labels argmax 与无羽化一致；48 行高栅格滑窗隔离；
      NoData 全波段；uncertainty 槽独立）——先证旧实现失败
- [ ] 修复：槽位独立分配 + 权重按 slot 记账（per-slot weight）
- [ ] GREEN ×2 双跑（锁内 -j4）
- [ ] 受影响族回归：test_model_runtime_9（M5/#r18B 单通道羽化不回归）

## C2 [#1554/#1553] 会话池/ChunkGraph 稳健性 — 已 FIXED_OPEN
- [x] 源码级验真（d9f1b886f1 在 master 内，行为已符合 issue 建议修复方向）
- [x] 归档证据（ISSUE_TRIAGE.md）；不重复实现；PR body 建议随修复提交关闭

## 交付（本 PR）
- [ ] REVIEW_R1（独立 subagent 反对式审查）→ 修复 P0-P3
- [ ] REVIEW_R2（另一独立 subagent，复核 R1 修复引入新错）→ 修复
- [ ] TEST_RESULTS.md / PERF_BASELINE.md / CROSS_TRACK_FINDINGS.md / FINAL_REPORT.md / PR_BODY.md
- [ ] push 前重查 open PR/远端 SHA → push 分支 → gh pr create（真实 URL 才算完成）

## 里程碑后续（本 PR 不承诺，记入 FINAL_REPORT 展望）
- C1（模型/数据/输出语义统一审计）、C3（分块流水线原子产物）、C4（可观测）、C5（确定性/吞吐基准）、
  C6（极端路径攻防）：C0 交付后按同循环逐包推进；GPU 实测项本机无 CUDA，全部 NOT_EXECUTED 标注。
