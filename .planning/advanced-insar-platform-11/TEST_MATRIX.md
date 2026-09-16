# TEST_MATRIX — 能力 → 独立 oracle → 命令 → exit → 证据

所有测试为独立真值（解析解/独立实现/不变量），不复用被测实现造绿灯。执行统一
`QT_QPA_PLATFORM=offscreen ctest --test-dir build-dev -R <family> -j1`。

| ID | 能力 | 独立 oracle | 测试文件/命令 | exit | 证据 |
|---|---|---|---|---|---|
| A-1 | 轨道插值真值（既有回归） | 圆轨道解析位置/速度 | `ctest -R test_sar_orbit` | （回填） | |
| A-2 | pair 真值：波长不一致拒绝 | 独立构造 λ1≠λ2 → 期望 typed refusal | tests/test_sar_baseline.cpp | | |
| A-3 | pair 真值：轨道无效拒绝 | 单状态/时间乱序/NaN 分量 | tests/test_sar_baseline.cpp | | |
| A-4 | pair 基线 B∥/B⊥ | 平行/垂直构造几何解析值 | tests/test_sar_baseline.cpp | | |
| B-1 | 地形相位解析真值 | 平地（h=0）双圆轨道 → φ_topo 与解析 −4πΔr/λ 逐像元比对（容差 1e-6 rad 量级） | tests/test_sar_topographic_phase.cpp | | |
| B-2 | 去地形相位残差 | 已知 DEM 坡面 + 合成干涉（同几何生成）→ 去除后残差 |RMS| < 预设容差；含纯地形分量像元处 | 同上 | | |
| B-3 | 元数据缺失 fail-closed | 缺轨道/缺 λ/缺 DEM → typed refusal | 同上 | | |
| B-4 | NaN/NoData 传播 | DEM NoData 像元 → 输出 NaN，不产生伪值 | 同上 | | |
| C-1 | 局部偏移场已知答案 | 分块平移场（左半 dy=+2, 右半 dy=−1）→ 估计场恢复（±0.25 px） | tests/test_sar_coregistration.cpp | | |
| C-2 | warp 重建 | 已知平移 slave warp 回 master → 与原场逐像元误差 < 容差；边界 NaN 语义 | 同上 | | |
| C-3 | 置信/退化 | 均匀纹理缺失区 conf 低于阈值 → fallback 计数暴露 | 同上 | | |
| D-1 | builtin 解缠回归 | 既有 test_sar_insar.cpp 全绿（不回退） | `ctest -R test_sar_insar` | | |
| D-2 | 外部 provider 正例 | 假 provider（测试内置 C++ 小可执行）恒等/加 2π 变换 → 输出正确 | tests/test_sar_unwrap_provider.cpp | | |
| D-3 | 缺 bin fail-closed | 不存在的 providerBin → UNWRAP_PROVIDER_UNAVAILABLE，无残留文件 | 同上 | | |
| D-4 | 崩溃 provider | exit(3) 的假 provider → UNWRAP_PROVIDER_FAILED，无半成品 | 同上 | | |
| D-5 | 超时 | sleep 假 provider + timeoutSec=1 → UNWRAP_PROVIDER_TIMEOUT，进程被杀 | 同上 | | |
| D-6 | 输出校验 | 截断/含 NaN 输出 → UNWRAP_PROVIDER_INVALID_OUTPUT | 同上 | | |
| D-7 | 取消 | cancel 置位后 unwrap → Cancelled，临时目录清理 | 同上 | | |
| E-1 | pair 图已知答案 | 3 场景全对 + maxPerp 过滤 → 预期 pair 集 | tests/test_sar_pair_network.cpp | | |
| E-2 | 波长不一致 fail-closed | 混 λ 场景 → typed refusal | 同上 | | |
| E-3 | 断连 | 两孤立子图 → 默认 refusal；allowDisconnected → 分量标注 | 同上 | | |
| E-4 | 相位闭合恒等 | 同栈三 SLC 闭合相位 ≈ 0（模 2π）；注入位移 → 偏离 | tests/test_sar_phase_closure.cpp（并入 E 测试亦可） | | |
| F-1 | 线性位移场恢复 | 已知每 epoch 位移表（含缺测对 + 相干权重）→ 反演位移/速度解析容差 | tests/test_sar_network_inversion.cpp | | |
| F-2 | 断连分量 | 孤立 pair 分量 → NaN + isolated 计数，参考分量正常 | 同上 | | |
| F-3 | pattern 上限 | 高构造 distinct NaN patterns → typed refusal | 同上 | | |
| G-1 | capability drift | `ctest -R test_capability_drift` | | | |
| G-2 | algorithm meta drift | `ctest -R test_algorithm_meta_drift` | | | |
| G-3 | contract 快照 | `ctest -R test_contract_platform_9`（再生后字节一致） | | | |
| H-1 | 全链 known-answer | 合成轨道+DEM+位移 → 干涉→去地形→解缠→反演，速度恢复容差 | tests/test_sar_platform11.cpp | | |
| H-2 | 取消/清理贯通 | 长链 mid-cancel → Cancelled + 无 .tmp~ 残留 | platform11 + provider 测试 | | |
| H-3 | 有界规模 | 反演 epoch 上限 refusal；pattern 上限 refusal（逻辑上限，非 wall-clock） | network_inversion 测试 | | |

## 连续两遍验证（Phase 8）

命令组（Phase 8 固化）：`ctest -R "sar|capability_drift|algorithm_meta_drift|contract" -j1`
连续两遍 exit 0 → 记入 EVIDENCE。
