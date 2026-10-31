# R25-B TASKS

## W0 侦察与基线
- [x] 远程实查（PR/Issue/branch）、worktree/分支建立、.gitignore allow-list
- [x] 三深挖点缺口实码验证（导入预检/推理计划/差异原因）
- [ ] 基线构建 + 邻域测试基线绿（排队中：兄弟轨持锁）

## W1 F1 导入预检解释报告
- [ ] 新模块 src/data/import_preflight.{h,cpp}：ImportPreflightReport schema v1
- [ ] 接生产路径：ProductAdapterRegistry/assetIdentityToken/DataManager dedup 前置
- [ ] tests/test_r25_b_import_preflight.cpp（RED→GREEN×2）
- [ ] 文档 docs/datasets/import-preflight.md

## W4 F2 推理计划与质量摘要
- [ ] src/operators/runtime/inference_plan.{h,cpp}：planModelInference + InferencePlan
- [ ] 质量摘要 InferenceQualitySummary（从 stats 构造，类型化）
- [ ] tests/test_r25_b_inference_plan.cpp（RED→GREEN×2）
- [ ] 文档 docs/inference/plan.md

## W5 F3 复现差异原因报告
- [ ] replay_deviation v2：DeviationEntry{dimension,domain,detail,remediation}
- [ ] tests/test_r25_b_replay_deviation_v2.cpp（RED→GREEN×2）
- [ ] 文档 docs/experiments/replay-deviation.md

## W2/W3/W6/W7 定向加固（按账本差距，时间允许）
- [ ] W2: 远程引用在 F1 中的拒绝语义 + 本地 mock（如需）
- [ ] W3: DatasetStore 冲突码可观测性核查（#1719 已修门，仅核对回归钉）
- [ ] W6: F2 plan 的瓦片网格预算 vs 引擎实际（metamorphic 对照，如可达）
- [ ] W7: E2E：F1→registerSource→F2 plan→runModelInference→ExperimentStore→F3（CLI/服务级）

## W8 评审/回归/PR
- [ ] R1 → fix → 回归
- [ ] R2 → fix → 回归
- [ ] 注册完整性门 + diff --check + fetch 复核
- [ ] push + PR + FINAL_REPORT
