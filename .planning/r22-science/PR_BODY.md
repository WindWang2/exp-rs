# fix(r22-science): 科学算法数值正确性与跨平台确定性收敛（#1707 #1684 #1685 #1686）

R22-01 science 轨道。base `master@911e0b17a3533ca0d79c1b68b000d6052c4c3c61`，
head `agent/r22-science-20261025T133250Z`（6 commits）。RED→GREEN→GREEN×2
全部落测试；R1/R2 两轮真正独立的对抗评审闭环（两位评审各自以独立
MT19937/暴力 oracle 复算并构造反例，非开发自评）。

## 修复清单

| Issue | 缺陷 | 修复 |
|---|---|---|
| **#1707** (P1) | F-H 精确欧氏距离变换下包络求值原地覆盖 f，后续 f[v[j]] 读到已变换值，`rs:proximity` 距离系统性偏小（3×4 反例 (3,0) 得 √6 应为 √8） | `squaredDistancePass` 求值写入独立输出缓冲；行/列 pass 各增一条 O(n) scratch |
| **#1684** (P1) | `computeStatsFromAccumulators` 把均值截断为 float 再求 sumSq/N−mean²，大偏移小方差严重失真（10000.3±0.5 σ 偏 +17%；1e6±0.25 报 64 应为 0.25） | 均值全程 double（Sterbenz 精确相减）；`math_utils.h` 写明双重误差下限契约；OBIA `rs_segment_features` 累加器加 Kahan 补偿 |
| **#1685** (P1) | 多处 seed=42 路径用 std::shuffle/uniform_int/normal_distribution——算法标准未规定，跨 STL 不可复现（教学 Windows 机 vs Linux CI 结果不同） | 新增 Qt-free `portable_random.h`（mt19937 引擎 + 本文件固定的 Fisher-Yates/u64/u53/Box-Muller，draw 顺序显式定序）；替换 9 个调用点（split×3、utils、training、multires、kmeans reservoir、PPI kernel+operator 同序列、RANSAC） |
| **#1686** (P2) | Resampler 以像元中心为足迹边界，最外半像元环（有效覆盖）被置 NoData，上采样/亚像元平移每边丢一行列 | 足迹改为栅格边缘 [-0.5, w-0.5]×[-0.5, h-0.5]，内部边缘钳制采样；同步修正 `resampler.h` 公有契约文档 |

## RED→GREEN 证据（详见 `.planning/r22-science/RED_GREEN.md`）
- RED：修复前 `test_r22_science_numerics` 4/7 FAIL（含 issue 原反例 2.449≠2.828、
  σ=64≠0.25）；`test_r22_science_resampler` 4/5 FAIL。R1/R2 评审又分别以独立编译的
  修复前 TU 复现了 6≠8、0.583961、64.0005 ——RED 真实性三方确认。
- GREEN×2：3 个新测试二进制 + 10 个受影响既有族两轮全绿；核心对拍
  **351,467 断言**（3200+ 随机 mask vs 暴力 O(N²)）零错误。

## 独立评审
- **R1**（APPROVE，1×P2 + 8×P3，`REVIEW_R1.md`）：评审员用 from-scratch Python
  MT19937 复算全部黄金值、跑 2 万 mask 对拍、补建并运行 11 个风险测试二进制。
  9 项发现全部闭环（commit eb41870），含 `resampler.h` 公有契约漂移（P2）、
  `boundedBelow(0)` UB 守卫、range 版 Fisher-Yates 黄金 pin 等。
- **R2**（APPROVE，0×P0/P1/P2 + 2×P3，`REVIEW_R2.md`）：强制反例构造——
  111,022 mask 对拍（51,002 例按位精确，45M 像元零错）、1,275,884 点重采样扫描
  （NoData 区域=足迹补集，环内 only-adds）、第二个独立 MT19937 复算、
  gcc-16 vs clang-23 交叉编译验证、33 个测试二进制（含 9 个此前无人跑过的）全绿。
  2 项 P3（模偏差上界表述、方差双重下限表述）已闭环（commit 1b95ec3）。
- 开发过程中的自检缺陷同样记录：黄金值首版受"同表达式双 rng() 求值顺序未指定"
  影响出错——恰为 #1685 要消灭的缺陷类别，已显式定序并由黄金测试钉死。

## 与 R21/R22 兄弟轨的差异核对
- **#1676（R21-A，open）**：源码级排重确认其未修 #1684（同函数在 PR head 仍为
  float 均值）与 #1685 kmeans 点；本轨与它在 `math_utils.cpp`（不同函数：
  computeStatsFromAccumulators vs bothNormalizable）和 `rs_kmeans_operator.cpp`
  （不同区域：reservoir vs scaler）轻度交集，git 可自动合并，语义无冲突。
- **#1673/#1674/#1675/#1677/#1708/#1709/#1710/#1711**：源文件零交集；仅
  `tests/CMakeLists.txt`/`.gitignore` 各自 append-only 追加（同 R21 惯例）。
- **#1709（r22-inference）已实施 #1706**，本轨未重复（见 CROSS_TRACK_FINDINGS）。
- R20 已合并科学数值回归（#1671 等）在基线上全绿，无 merge-loss。

## 性能
无性能主张（`PERF_BASELINE.md` = N/A + 各修复的结构性开销说明：EDT 每行/列
一条额外 scratch；Kahan 每像元每波段 4 次双精度加减；RNG 与 std 实现同阶）。

## 遗留 / 未执行
- 全仓 ctest 未跑（按最窄→受影响族→集成策略；R1/R2 又补跑 20+ 二进制）。
- Windows/macOS 实机未跑（单主机）：跨平台主张基于标准条款（mt19937 引擎算法
  被 [rand.eng.mersenne] 唯一规定；被替换的 shuffle/distribution 算法未规定）
  + 黄金序列锚点（任何 STL 偏离立即 RED）；本机做了 gcc-16 vs clang-23 交叉验证，
  libc++ 在宿主不可用（已在 REVIEW_R2.md 声明）。
- #1687（P3）：-999 魔数改波段声明 NoData 需动签名与全调用方，随 #1676 合并考虑；
  validCount 语义已文档化。src/app 朴素累加残余风险已记 CROSS_TRACK_FINDINGS。
- **不等待 CI**：线上工作流不作为完成门，本地证据如上。

## 文件冲突排查
`git diff --name-only 911e0b17..HEAD` 全部在允许范围（src/processing/algorithms/**、
src/analysis/**、src/operators/rs/**、tests/**、.planning/**、.gitignore 白名单
stanza）。R1/R2 各自独立核对了白名单与 append-only 性。

Closes #1707, closes #1684, closes #1685, closes #1686.
