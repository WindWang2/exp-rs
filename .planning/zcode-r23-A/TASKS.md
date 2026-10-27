# r23-A TASKS

状态机: PENDING → RED → FIX → GREEN1 → GREEN2 → REVIEWED
来源标记: [I]=issue 池, [A1]=时序审计代理, [A2]=高光谱审计代理, [A3]=地形/指数审计代理, [M]=主会话人工核验

| ID | WP | 任务 | 来源 | 验收结论 | 状态 |
|---|---|---|---|---|---|
| T1 | A02 | bothNormalizable 去 -999 魔数 + 同值负反射率对→0(#1687-1);validCount 语义文档(#1687-2) | [I] | test_math_utils 新 SECTION:负反射率对→0、<-999 有限值→计算值、(0,0)/(x,-x)→NaN、非有限→NaN;RED→GREEN×2 | RED 测试已写 |
| T2 | A07 | viewshed 环扫描亚米格网断链(F1,P1):ring 按 min(cellSize) 量化保证 toward-observer 前驱环严格更小 | [A3][M] | test_terrain_viewshed:0.3m/各向异性 0.25×0.8 平面全可见;RED→GREEN×2 | RED 测试已写 |
| T3 | A07 | 曲率混合二阶导模板错误(F2,P2):(g+i−a−c)→(a+i−c−g);header 公式同步 | [A3][M] | test_terrain_foundation5:saddle z=xy 中心 profile=+1/plan=−1;各向异性 8/17;RED→GREEN×2 | RED 测试已写 |
| T4 | A05 | spectral_roi 忽略 WAVELENGTH_UNITS(H1,P2):µm→nm 归一化(经 SpectralWavelength::normalizeToNm) | [A2][M] | test_spectral_roi:µm 栅格→490/665nm;RED→GREEN×2 | RED 测试已写 |
| T5 | A02 | 流式 linearStretchApplyTile ±Inf 当数据(F3,P3):与全帧 isfinite 契约对齐 | [A3][M] | test_image_enhancement:tile==frame,Inf→nodata;RED→GREEN×2 | RED 测试已写 |
| T6 | A07 | sunPositionDeg 极点 NaN 方位返回成功(F4,P3):极区极限 az=acos(∓cosH) | [A3][M] | test_terrain_solar:±90° 有限方位(180/0/270±2°);RED→GREEN×2 | RED 测试已写 |
| T7 | A03 | harmonicFit 谐波阶数 clamp 不一致(T1a,P3):入口统一 clamp 1..6,消 OOB/未初始化读 | [A1][M] | test_temporal_fit:7≡6(逐系数)、0≡1、13/3 项;RED→GREEN×2 | RED 测试已写 |
| T8 | A05 | MNF reconstructionRmse 空选择语义与 inverse 矛盾(H3,P3):空=全选→RMSE 0 | [A2][M] | test_mnf_transform:空选择 RMSE=0 且≡全枚举;RED→GREEN×2 | RED 测试已写 |
| T9 | A05 | VCA 退化门限绝对阈值非尺度无关(H4,P3):相对首锚点 norm² | [A2][M] | test_spectral_unmixing_fcls:1e-7 尺度单纯形可提取;RED→GREEN×2 | RED 测试已写 |
| T10 | A05 | spectral_table digestHex/toJson 宽度不一致 OOB(H5,P3):typed refusal | [A2][M] | test_spectral_table:不一致表→空 digest/空 JSON;RED→GREEN×2 | RED 测试已写 |
| T11 | A05 | spectral_anomaly.h 协方差 N−1/N 契约漂移 + ridge 文档(H2,P3,仅文档) | [A2][M] | header 与实现一致(N−1、ridge=max(1e-9,tr/B·1e-7));无行为变化 | PENDING |
| T12 | A01 | 审计 CLEAN 面登记(时序代数/高光谱核对/SAR polsar 人工/地形 D8/D∞ 等) | [A1][A2][A3][M] | 记录入 FINAL_REPORT | PENDING |
| T13 | A10 | 全部受影响测试族两轮 GREEN + 同域回归(含 e2e/smoke 邻族) | 全部 | TEST_RESULTS.md 记录命令+退出码 | PENDING |
| T14 | 交付 | R1/R2 独立评审、push、PR、FINAL_REPORT | 全部 | O0-O8 | PENDING |

## 不修(证据不足/超范围)
- [A1] seasonal breaks CI 统计量与点估计可能不同(T2a suspicious):未构造出反例,登记 RISK_REGISTER。
- [A1] harmonicFit robust rmse/r2 加权/未加权混合(T3a suspicious):header 未钉语义,登记 RISK_REGISTER。
- [A2] Dictionary::etUnit 死标签:非数值缺陷,不修。
- #1687-2 validCount:随 T1 顺手文档化(见 T11 同类)。
