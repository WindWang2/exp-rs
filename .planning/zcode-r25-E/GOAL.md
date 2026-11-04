# GOAL — R25-E 本科遥感实验、LabSpec评分反馈与科学验证闭环深化

- BASE: 2610cb4741e69a89124921e44c03e340e2522ee2 (origin/master, 2026-10-27)
- Branch: agent/r25-E-teaching-grading-verification-20261028T170411Z
- Worktree: /home/kevin/projects/rs-studio/exp-rs-r25-E-20261028T170411Z

## Oracle（任务书 §7 七项验收）
1. ≥2 完整场景 E2E：学生 NDVI foundry 运行→科学验证→rubric→反馈；教师多学生批量评分→JSON+CSV 一致发布→异常查阅。
2. ≥3 项现有教学功能深化（LabSpec/步骤操作、评分/科学验证、教学管理 UX），非空配方或标题计数。
3. Rubric canonical schema 在编辑器、评分引擎、文档、实际评分结果上逐字段一致；规则/工件/版本 hash 变化使旧 checkpoint 正确失效。
4. 跨学生隔离、非法路径、深 JSON、NaN/Inf、缺工件/不可检验 fail-closed。
5. 教师界面发现/批量评分可取消、不阻塞 GUI 线程（#1695-17c 主缺陷已修 by r24-c；残留 close-time join + CT-C6 双扫描为本轨目标）。
6. 测试覆盖 2 套互斥权威评分规则（fixture ndvi_basics vs foundry ndvi_landsat）+ ≥4 类对抗失败情境；GREEN×2。
7. 不改 A/B/C/D 公共所有权实现；教学页面只用既有 API。

## 完成定义
真实源码/测试/文档变更 → R1/R2 独立评审闭环 → push 分支 → 创建 PR → 停止（不 merge、不等 CI）。
