# fix(r23-A): 遥感科学算法数值正确性与 NoData 语义闭环 — 10 项修复 + dNBR legacy 缝(RED→GREEN×2,双轮独立评审)

r23-A 轨道。BASE `master@5fe642ec6339807823aa56612ba94668592a738d`(执行期 master 前移至 5fe642ec63 后已 merge,仅 .gitignore 白名单并集),HEAD `7f4778b0fce8ca47afa8653c671c28af814fbe45`(19 commits:10 算法修复 + 1 契约文档 + 1 dNBR 缝 + 2 评审处置 + 4 planning/证据 + 1 merge)。tests/CMakeLists.txt 零改动(append-only 纪律,与 origin/master 逐字节一致)。

## 修复清单(全部 master-RED → HEAD-GREEN×2 → R1/R2 复核)

| # | 严重度 | 模块 | 缺陷 | 反例(实测) |
|---|---|---|---|---|
| 1 | P2 | math_utils #1687 | bothNormalizable 魔数 ≤−999 + 等值负对→NaN,毁掉大气校正后的合法负反射率 | NDVI(−0.01,−0.01)=NaN 应为 0;(−1500,−500)=NaN 应为 0.5 |
| 2 | **P1** | terrain_viewshed | 环扫描按整地图单位量化,亚米格网(csx<1)同环邻居无合法前驱,平地大片假不可见 | 0.3m DSM 平面 21×21:可见 75/441 |
| 3 | P2 | terrain_analysis | 曲率混合二阶导模板 (g+i−a−c) x-翻转对称,非混合偏导 | z=x·y 中心 profile=+2 应为 +1;z=x²y R1 反例 12.8 应为 9.6 |
| 4 | P2 | spectral_roi | WAVELENGTH_UNITS 被忽略,µm 栅格波长轴 1000× 偏小 | 0.49µm 报为 0.49 应为 490nm |
| 5 | P3 | image_enhancement_streaming | linearStretchApplyTile 只滤 NaN,±Inf 饱和成 255/0 有效值(全帧 oracle 写 nodata) | +Inf→255 ≠ frame nodata |
| 6 | P3 | terrain_solar | 极点方位角 = 消噪声相除的舍入噪声却返回成功 | lat=±90:171.2°/8.8°,物理极限 180°/0° |
| 7 | P3 | temporal_fit | harmonicFit 阶数 clamp 分裂:terms 用 clamp、design 循环用原始值 | harmonics=7 越界写(SIGABRT);=0 读未初始化 |
| 8 | P3 | mnf_transform | reconstructionRmse 空选择=全丢弃,与 inverse 的"空=全选"矛盾 | 空选择 RMSE 0.1174 应为 0 |
| 9 | P3 | spectral_unmixing | VCA 退化门绝对 1e-12,拒绝小量级(1e-7)合法单纯形 | 2e-7 尺度三顶点立方体拒绝 |
| 10 | P3 | spectral_table | digestHex/toJson 宽度不一致表 OOB 读(UB) | 越界哈希非空,应为 typed 拒绝 |
| 11 | — | rs_spectral_index_operator | r21-A M3 冲突解决:dNBR 未声明 −9999 哨兵防护移至具名缝 legacyNegativeSentinelFree | 邻域回归发现 M3 钉旧规则→分层解决;缝回退→端到端测试 RED(变异验证) |

## 证据(.planning/zcode-r23-A/)
- **RED**:10/10 新用例在 master 源码恰好失败、既有用例全绿(命令+退出码见 TEST_RESULTS.md;temporal 为 stack-protector SIGABRT 铁证)。
- **GREEN×2**:12 直接族两轮断言数逐一相同(10,972 断言);邻域 13 族全绿(含 change_detection 888,684 / band_math 130,277 断言);merge master 后复跑 12 族全绿。
- **双独立评审**:R1 APPROVE(4 次变异 RED 复现 + 2 个独立反例 z=x²y 曲率 9.6、南极 315°;1×P2 已修:seam 端到端钉 + 变异验证);R2 APPROVE(不继承 R1:18 组对抗格网 17,280 格不变量实测 0 破、极区带缘跳变 0.28°、VCA 退化余量 20 个数量级;2 次变异)。全部 P0-P2 闭环,P3 修/登记齐全。
- **性能**:无性能主张(全部修复零复杂度变化;viewshed 环索引内存有界论证见 REVIEW_R1 §2)。

## 架构披露(#1687 设计决策,R2-P2-1/R13)
声明 NoData 是唯一权威:未声明 −9999 族哨兵在 dNBR 缝受 legacy 筛保护,其余指数分支按 #1687 如实计算(R2 反例 NDVI(−9999,0.3)→+1.0000600,master 因魔数意外防护)——遗留产品应声明 NoData。**回退说明**:cc34cb4ed6 与 d4f284c6b4 需成对回退。

## Issue 状态
- 修复:#1687(open,待维护者验证关闭)
- 复核为已修未关(不重复修):#1589/#1549/#1631×3/#1646-6/#1550/#1538/#1590/#1591(逐条源码证据见 ISSUE_TRIAGE.md)
- 范围外登记:#1646-1..5/#1642×4/#1588 等

## 复现
```bash
cmake -S . -B build-r23-A -DCMAKE_BUILD_TYPE=Debug -DENABLE_TESTS=ON
flock $HOME/.cache/exp-rs-build.lock cmake --build build-r23-A --parallel 2 --target test_math_utils test_terrain_viewshed test_terrain_foundation5 test_spectral_roi test_terrain_solar test_temporal_fit test_mnf_transform test_spectral_unmixing_fcls test_spectral_table test_image_enhancement test_image_fusion test_spectral_formula_drift
QT_QPA_PLATFORM=offscreen ctest --test-dir build-r23-A --output-on-failure --parallel 1 -R "test_(math_utils|terrain_viewshed|terrain_foundation5|spectral_roi|terrain_solar|temporal_fit|mnf_transform|spectral_unmixing_fcls|spectral_table|image_enhancement|image_fusion|spectral_formula_drift)"
```
不等待 CI;不自动合并。
