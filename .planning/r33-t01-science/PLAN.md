# R33-T01 PLAN — 科学数值、缺测语义与有界算法

Base: origin/master 56f0bd2a0a6a05827c8a40770924b445a0f99862（#2134–#2137 已合入，排他路径解除，经 master 继承）

## 状态总览（2026-11-17T15:05Z 刷新）

- 无开放 PR；#2138 关闭未合并（内容已被吸收，不复活）。
- 本轨候选 issue：#2127 #2126 #2128 #2102 #2129 #2091 #2094 #2095 #2088 #2100 #2092 #2093 #2096 #2097 #2101 #2105 #2103。
- 优先级（按 prompt）：核级已复现的 PolSAR(#2127) → 影响科学结果的 NoData/SAR/尺度(#2126/#2128/#2102/#2129/#2092) → 早期拒绝与内存边界(#2088/#2100/#2093/#2096/#2097) → 其余(#2091/#2094/#2095/#2101/#2105/#2103)。

## WP01｜PolSAR 左右旋与物理功率（#2127）— IN PROGRESS

问题：yamaguchi4 用 signed fh/4 扣除 helix 贡献；左旋(fh<0)时 fv/r11/r33/r13 全部向错误方向移动，功率膨胀（0.6→2.2）。物理模型：左右旋对角贡献均为 |fh|/4，Re C13 均为 −|fh|/4；符号只存在于 Im C12/C23。

改动集：
1. `src/processing/algorithms/sar/sar_polsar.cpp` yamaguchi4：扣除项统一 |fh|/4；注释说明。
2. `src/processing/algorithms/sar/sar_polsar.h`：模型注释 trace=|fh|·3/4、fv=1.5·(C22−|fh|/4)。
3. `docs/processing/sar-domain.md` §7.3：同步公式。
4. `tests/test_sar_polsar.cpp`：左旋 fixture 改物理 PSD（u_left·u_left^H），补非负分量、trace3=0.6、守恒断言。
5. 新文件 `tests/test_r33_t01_polsar_helix.cpp`：
   - 核级：u=(√.2,±j√.2,−√.2) 双旋 PSD 构造（accumulatePolSample+finalizeEnsemble）→ 相同非负分量、trace3=0.6、四分量和=trace、helix=0.6 其余 0。
   - 核级：左旋+surface+volume 混合目标精确恢复 + 守恒。
   - 核级：强 helix 估计使 fv 负 → clamp 0（文档化 SPAN break）；坏矩阵有限输出。
   - E2E：真实 CFloat32 GTiff（左旋场）→ rs:sar_polsar_decompose yamaguchi windowSize=1 → 重开 5 band 断言 helix=0.6/volume=0/surface=0/double=0/span=0.6。
6. `tests/CMakeLists.txt`：追加最小注册块。

Oracle（独立推导）：
- u_right=(√.2, j√.2, −√.2): C12=C23=−j·0.2 → fh=+0.8；u_left 为其共轭 → fh=−0.8；两者 c11=c22=c33=0.2, C13=−0.2, trace=0.6。
- 分解：fv=max(0,1.5·(0.2−0.2))=0；r11=r33=0.2−0.2=0；r13=−0.2+0.2=0 → surface=double=0；helix=|fh|·0.75=0.6；和=0.6=trace ✓（现 HEAD 左旋给 volume=1.6、和=2.2 ✗）。
- 混合（fs=2, β=0.3+0.1j, fv=1, fh=−0.8）：surface=2·(1+0.1)=2.2, volume=8/3, helix=0.6, double=0, 和=trace。

验证：test_sar_polsar + 新 test_r33_t01_polsar_helix（CTest 真实注册）；构建 -j4 双锁。

## 后续 WP（本检查点后按序推进，逐个先写 oracle）

WP02 geocode beam-away(#2126)、WP03 network inversion(#2128)、WP04 finite sentinel(#2102)、WP05 speckle(#2129/#2103-6)、WP06 landform(#2091)、WP07 hillshade(#2094)、WP08 resolveFlats(#2095)、WP09 median(#2088)、WP10 BandMath(#2100)、WP11 Landsat(#2092)、WP12 PCA/MNF(#2093)、WP13 MAD(#2096)、WP14 VCA(#2097)、WP15 spectral source(#2101)、WP16 provenance(#2105)、WP17 apply_mask(#2103-1)、WP18 stats(#2103-2-4)、WP19 temporal(#2103-5)、WP20 CC/sieve bounded。
