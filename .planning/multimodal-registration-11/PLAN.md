# PLAN — multimodal-registration-11

Baseline：`origin/master @ a5b11b7f10`，worktree `../exp-rs-multimodal-registration-11`。

## 核心架构决定（详细理由见 DECISIONS.md）

新建 `src/processing/algorithms/registration/` 子目录（namespace `rs::registration`），消费既有
`rs::algorithms`（GeometricTransform / GcpManager / Resampler / TpsInterpolator / FeatureMatcher-RANSAC）
稳定 seam，**不复制不改写**既有实现。RPC bias 数学放 `registration/rpc_bias_model.*`（纯函数层），
`src/analysis/georeferencing/qgsrpcgcptransformer` 改为消费该层（additive、向后兼容）。

## Phase → 交付

| Phase | 交付 | 关键文件 |
|---|---|---|
| 1 | 契约层：MatchPair/MatchCandidate、TransformEvidence、QualityReport、StackSolution 数据模型 + FFT/2D 通用工具 + CMake 接线 + 契约测试 | `registration/registration_types.{h,cpp}`, `registration/fft2d.{h,cpp}` |
| 2 | A+B：masked phase correlation、gradient/RANK patch 描述子、MI 窗口度量、金字塔 coarse-to-fine loop、coverage 约束、失败=低置信 refusal | `registration/multimodal_matcher.{h,cpp}` + tests |
| 3 | C+D+E：model selector（holdout CV + 改进门限）、RPC bias（constant→affine、CV 选择、高度敏感性）、stack registration（pair graph + 全局 adjustment + 闭环） | `registration/model_selector.*`, `registration/rpc_bias_model.*`, `registration/stack_registrator.*` + qgsrpcgcptransformer 接线 + tests |
| 4 | F+G：quality products（CE90/残差场/局部置信度/report JSON）、agent tool 注册 + `multimodal_register`/`stack_register` action、`rs:register_images` + `rs:stack_register` 算子、#1005 fail-closed 修复、docs/ADR/CHANGELOG | `registration/registration_quality.*`, `src/agent/*`, `src/operators/rs/*`, `src/app/georeferencer/qgsgeoref_shell_window.cpp`, docs |
| 5 | 硬化：cancel token 贯穿、cap/下采样降级、内存上限、Unicode path、atomic sidecar 写出 | 上述文件迭代 + PERFORMANCE.md |
| 6 | E2E：synthetic warp fixtures（H）、known-answer 全链路、negative/refusal、drift golden | `tests/registration_test_helpers.h`, `tests/test_registration_*` |
| 7 | Subagent #2 对抗 review + P0/P1 修复 | REVIEW_LOG.md |
| 8 | 双遍验证、rebase、push、PR | PR_BODY.md |

## Oracle 追踪

GOAL Loop Oracle 7 条 → TEST_MATRIX.md 逐条映射验证命令与证据。
