# TEST MATRIX — multimodal-registration-11

Oracle 独立性原则：真值全部手算/解析/文档化常量；禁止复用被测实现造绿。命令均 `QT_QPA_PLATFORM=offscreen`、`ctest -R <X> -j1`。

| # | 能力（Pkg） | Oracle | 测试文件 | 命令 | exit | 证据 |
|---|---|---|---|---|---|---|
| 1 | 2D FFT 正确性（B 基础） | 解析：常数/正弦频谱位置与幅值 | test_registration_fft | ctest -R test_registration_fft | 0 | Phase 8 双遍 |
| 2 | phase corr 已知平移（B） | 文档化平移 (dx,dy)，含亚像素 | test_multimodal_matcher | ctest -R test_multimodal_matcher | 0 | Phase 8 双遍 |
| 3 | SAR-like 跨模态窗口匹配（A/B） | 已知变换 + 乘性斑点/单调辐射变换下仍锁定；无结构场景 refusal | test_multimodal_matcher | 同上 | 0 | Phase 8 双遍 |
| 4 | coverage 聚类拒绝（B/Oracle#2） | 全部 tie-point 挤在 2 个格 → coverage 不足 → 低置信/refusal | test_multimodal_matcher | 同上 | 0 | Phase 8 双遍 |
| 5 | 模型选择已知真值（C/Oracle#1） | 纯平移数据选 translation；仿射数据选 affine 且 P2 被拒（过拟合）；手算 CV 方向性 | test_model_selector | ctest -R test_model_selector | 0 | Phase 8 双遍 |
| 6 | RPC bias constant vs affine 选择（D） | 手算 bias 场：常数场→constant；旋转场→affine；高度敏感性 dGround/dH 手算 | test_rpc_bias_model | ctest -R test_rpc_bias_model | 0 | Phase 8 双遍 |
| 7 | qgsrpcgcptransformer 向后兼容（D） | 既有 test_rpc_gcp_refine / test_rpc_transformer / test_rpc_golden 全绿不变 | 既有 | ctest -R "test_rpc" | 0 | Phase 8 双遍 |
| 8 | stack 全局调整闭合（E/Oracle#1） | 手构 3 景已知平移环：全局解恢复真值；断开一边闭环误差 = 已知值 | test_stack_registrator | ctest -R test_stack_registrator | 0 | Phase 8 双遍 |
| 9 | CE90/残差场/置信度（F） | 手算分位数（固定残差集）；聚类 GCP 置信度不被高估（Oracle#2） | test_registration_quality | ctest -R test_registration_quality | 0 | Phase 8 双遍 |
| 10 | agent 工具注册 + explain（G） | schema 校验 + 注册后 catalog 可见 + refusal 契约 | test_geometric_agent_tools（扩展） | ctest -R test_geometric_agent_tools | 0 | Phase 8 双遍 |
| 11 | rs:register_images / rs:stack_register E2E（G/Oracle#3） | 已知 warp 光学对；SAR-like 对跨模态；失败场景低置信产物不冒充成功 | test_registration_operators | ctest -R test_registration_operators | 0 | Phase 8 双遍 |
| 12 | #1005 fail-closed（G） | CRS transform throw → 不产生 GCP（UI 路径） | test_georef_shell_crs_failclosed | ctest -R test_georef_shell_crs_failclosed | 0 | Phase 8 双遍 |
| 13 | 全链路 e2e（H） | 合成 warp + 斑点 + outlier → warp 产物 vs 解析真值逐像素 | test_registration_e2e | ctest -R test_registration_e2e | 0 | Phase 8 双遍 |
| 14 | 既有 geometric 套件回归 | — | 既有 | ctest -R "test_gcp|test_geometric|test_feature_matcher|test_resampler|test_georef|test_image_warper|test_d14" | 0 | Phase 8 双遍 |

## Oracle 追踪（GOAL Loop Oracle → 行号）

1. 变换参数/点误差 closed-form 达标 → #2/#5/#6/#8/#13
2. 空间聚集 GCP 不被误报高质量 → #4/#9
3. 跨模态失败低置信/refusal → #3/#11
4. registration suites 两次通过 → #14 + Phase 8 双遍
5. diff/check/secret/生成物 clean → Phase 8 命令
6. 关键 targeted validation 连续两遍 → Phase 8
7. 独立 review P0=P1=0 → REVIEW_LOG.md
