# R21-E TASKS — 里程碑与可恢复工作单元

## M0 基线（✅ 完成 2026-10-23）
- [x] LIVE 盘点（BASELINE.md / ISSUE_TRIAGE.md / PR_COLLISION_MATRIX.md / OWNERSHIP.md / RISK_REGISTER.md）
- [x] E0 复测：R20-05 全部 13 项修复声明在 master 911e0b17 实锤（不重复修，PR 建议 close）
- [x] E0 #1601 考古：权威=src/verify（零生产消费）；生产评分走 agent OutputVerifier；projections.h 已备未接线
- [x] worktree 建立与 CMake 配置（5 次尝试补齐 GDAL/PROJ/GEOS/GSL/netcdf 路径）

## M1+M2 预防性测试 + 修复（进行中）
- [ ] W1 verifier 生产接线（#1601 主工作项）
  - W1.1 RED：测试「CLI lab 报告可携带科学验证证据」——现状 gradeForTeaching 无 verifier 证据（先证 RED）
  - W1.2 `src/verify_adapters/lab_verify_composer.{h,cpp}`：默认教学 spec（artifact.exists/type/grid）+ fs/grid probe 装配 + 三值状态投影（status/checks/spec_digest；Indeterminate 不折叠为绿）
  - W1.3 agent 胶水：LabGradeResult 增 `verifier` 字段（additive），gradeForTeaching 内调用组合器；toBodyJson 序列化；CLI/batch 零改动自动携带
  - W1.4 GREEN×2 + teaching/grader/verifier/lab 族回归
- [ ] W2 faultlab verifier 接线（#1613 完成）
  - W2.1 RED：schema 已声明 verifier 但 loader 拒绝（合法场景被拒 + 无钩子 fail-closed 行为缺失）
  - W2.2 faultlab 核：FaultRunOptions.verifierHook（依赖倒置）、runner 第 7 合取项、无钩子+有声明 → fail-closed + diagnostic
  - W2.3 loader 接受非空 verifier（严格校验：rules_ref 非空串、must_fail 元素非空、无未知键）
  - W2.4 studio_live 装钩子：FaultGrid→临时 GTiff 物化→OutputVerifier::gradeForTeaching(rules_ref)→must_fail ⊆ failed ids
  - W2.5 GREEN×2 + faultlab/studio 族回归
- [ ] W3 help/explain 域 P2/P3（#1614 #1615 #1616 #1639 #1636）
  - W3.1 #1614 RED：小预算 toJsonText 返回非 JSON → 修为最小合法 JSON（{} 兜底 + 截断舍入）
  - W3.2 #1615 RED：性能/重复建表 → 惰性缓存（首次构建后复用）
  - W3.3 #1616：numberToString 加 isfinite + 范围门（超出 → 科学计数法字符串，不走 ll 转换 UB）
  - W3.4 #1639-1/-2：help_presenter/terminology_provider 统一 truncateSummary
  - W3.5 #1636：删 explanation_validator 死成员 guidance_
- [ ] W4 弱断言扫描（#1558 延伸）：teaching/verifier/grader/faultlab/help 测试族 CHECK 恒真/弱断言扫描，可失败 oracle 替换

## M3 性能
- [ ] W5 #1615 性能基准（bridge 建表耗时 before/after）；批量评分 100/500/1000 份模拟提交吞吐+RSS（O-E2）

## M4-M5 端到端
- [ ] W6 Linux 端到端：bundle 素材 → 样例生成 → NDVI pipeline → lab --grade（断言 verifier 证据在场）→ grade_all 批量 → 审计报告可复现（O-E1/O-E5 部分；Windows 侧 NOT_EXECUTED 如实标注）
- [ ] W7 离线零 egress 证据：SICNU_OFFLINE=1 运行 + 环境审计（无法 strace 全程时给替代证据并标注限制）

## M6 评审
- [ ] R1 fresh reviewer（独立上下文）→ 修复 P0-P2 → 复测
- [ ] R2 fresh reviewer（再独立）→ 循环至零未决 P0-P2

## M7 交付
- [ ] PR_BODY.md、push、gh pr create（不自动合并、不等 CI）
- [ ] FINAL_REPORT.md
