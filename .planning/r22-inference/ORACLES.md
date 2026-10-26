# R22-03 Oracles（可验证完成条件）

## C0 (#1706) — 本 PR 的硬验收

1. RED：在未修复基线上 `test_r22_inference_feather` 失败（记录失败断言输出，证明复例真实）。
2. GREEN：修复后全部 5 个 TEST_CASE 通过，且**连续两遍**通过（ctest 双跑）。
3. `test_model_runtime_9` 全绿（单通道羽化 M5/#r18B 无回归）。
4. 科学断言：
   - 三类 feather 测试各类别概率**不相同**（band1 ≠ band3 于左核）；
   - 权重守恒（seam 处 (0.8·1+0.1·0.5)/1.5 精确到 1e-3 —— 权重只计一次的充要证据）；
   - 输出 argmax 与无 feather 参考在纯核一致（左 0 右 2），seam 翻转位置正确（x=14→0, x=18→2）；
   - uncertainty 槽 = softmax margin 独立解（≈0.2526）。

## 轨道级最终 Oracle（任务书 §7）

1. ✅（本 PR）#1706 三类 feather 测试：各类别概率不同、权重守恒、argmax 一致。
2. GPU 路径：本机无 CUDA —— 一切 GPU 实测项标 **NOT_EXECUTED**（不冒称测试过 CUDA）；CPU 契约测试真实执行。
3. 取消/重复运行无半成品：C0 范围内由 NoData/异常路径单测覆盖（写失败抛 typed error 已有断言）；更广 OOM/取消矩阵属 C2/C6 后续里程碑。
4. 端到端样例：`rs:infer`/`rs:segment` 链已由既有 test_model_runtime_9 / #1673 e2e 覆盖；本轨在 FINAL_REPORT 引用并在 PR body 说明（不重复造 e2e）。
5. ✅ 已先读 #1673 变更（head-role thresholding），证明非重复 PR（PR_COLLISION_MATRIX.md）。

## 交付 Oracle

- GitHub `gh pr create` 返回真实 PR URL/编号；不自动合并、不等 CI。
