# 科学算法有效域、无信息状态与光谱有效集合的真实化（R35-T01 切片1）

## 原问题与影响

六个当前 master 上可复核的科学合同缺口，均以源码行号证据在 `.planning/r35-t01-science/BASELINE.md` 立案：

1. **Landsat C2L2 反射率系数丢失（Refs #2092）**：真实 Collection-2 L2 MTL 以 `FILE_NAME_BAND_SR_Bn` 键携带波段，MTL 主路径发现的波段名保留 `SR_Bn`，而 `stackToGeoTiff` 的系数关联正则只认 `^B(\d+)$` → 标准 C2L2 SR 场景整景丢失 `SICNU_NUMERIC_SCALE/OFFSET` 仿射 stamp（指数/校准 consumer 读不到单位合同）；目录扫描 fallback 剥离 SR_ 反而能匹配，掩盖了主路径缺陷。
2. **PCA 文件 helper 晚失败（Refs #2093）**：`processPcaFile` 只拒 `>bandCount`；`<=0` 要先完整两遍像元扫描，再在 `GDALCreate`（非正 band 数）处以无关错误失败。内存版 `computePca` 与 rs:pca/rs:mnf operator 的既有合同是 `<=0 → 全部band`。
3. **MNF 文件 helper 晚失败**：`processMnfFile` 同样缺早归一；负值迟到 `Mtrans(numComponents,...)` 抛 `std::length_error`（无 typed 消息），0 迟到 create。
4. **MAD/IR-MAD 伪造无变化（Refs #2096）**：常量 before/after 图 trace=0 时 `std::max(eps,1e-12)` ridge 仍产出 ready 状态与 varMad=2 的"可信"统计，变换输出全 0 chi-square——正则化制造了无信息的置信度。
5. **VCA 被单个非有限谱毒化（Refs #2097）**：VCA 分支对全部像元求均值且无整谱 isfinite 过滤（PPI 内核有）；任一 NaN/Inf 像元使均值 NaN → 提取整体失败，即使存在足量有效谱。
6. **端元 seam 合同不一致 + FCLS QA 分母毒化**：typed `extractEndmembers` 的 PPI 分支可静默短返回（take<requested 仍 true），VCA 分支是"恰好 requested 或 false"；typed FCLS `meanSumConstraintViolation` 把非有限输入像元的 NaN abundance 累进均值 → 全图 QA 变 NaN，0 个有效贡献者被报成完美 0 违反。

## 已合并基线

base = master `120677e6`（含 #2142 科学修复、#2147 谱指数 claim、#2148 BandMath 预算、#2143/#2145/#2144）。本 PR 不重复任何已合并修复。

## 本次增量

- `satellite_products.cpp`：Landsat 系数关联正则放宽为 `^(?:SR_)?B(\d+)$`（ST_/QA 仍不参与反射率合同；混合堆栈维持仅 per-band stamp）。
- `image_enhancement.cpp`：`processPcaFile/processMnfFile` 在打开数据集后、任何扫描前把 `numComponents<=0` 归一为 bandCount（与内存 seam/operator 0=all 语义对齐）；`>bandCount` 早拒绝保持。
- `change_detection.cpp`：`madFinalize` 与 IR-MAD 迭代内在 ridge 前拒绝 `!(trace>0)`（含 NaN trace），typed 消息 "requires non-constant input: ... zero variance"；秩亏但非零方差、真实无变化（有方差同图）不受影响。
- `spectral_unmixing.cpp/.h`：VCA 增加整谱 isfinite 有效集合（均值/候选/退化门在有效谱上，输出仍为原始像元谱，有效数不足→false）；PPI 分支统一为恰好-requested-或-false；FCLS QA 仅对全有限 abundance 行求均值，0 贡献者→NaN（文档同步）。

## 边界与兼容

- 全部为库级/seam 修复，无公开 API 签名变化；`rs:pca`/`rs:mnf` operator 行为不变（其原本就先 clamp）。
- MAD 修复影响 rs_change_primitives/rs_change_streaming 消费方：它们已正确处理 false 返回（NaN 输出+错误消息）；未修改这两个 T04 所属 consumer 文件。
- typed 端元 seam 无产品调用方（仅测试），合同统一不破坏用户流程。

## 实际验证（本 HEAD，dev-default Debug，-j4 双锁）

- 新增 `tests/test_r35_t01_science_validity.cpp`（9 cases）与 `tests/test_r35_t01_landsat_affine.cpp`（4 cases）。
- 受影响旧回归：test_pca（8）、test_change_detection（41）、test_spectral_unmixing_fcls（8）、test_satellite_products（21）。
- 实际命令：双锁 guard 下 `ctest --test-dir build-dev -C Debug -j1 --output-on-failure`，第1遍 78/78（20.44s）、第2遍 91/91（22.74s）全部 Passed、exit 0。逐 case 名与分布见 `.planning/r35-t01-science/LEDGER.md`（本 PR 附带）。
- 限制：未跑完整 CTest 全集/Windows/Release（按轨协议记录，不宣称全绿）。

## base/head/review

- base：`120677e6e165d3e88bb9d2ec2a3fd98042ec719d`
- head：`621b6a2a1f`（R2 绑定；其后仅 docs-only 追加）
- R1/R2 独立审查记录：`.planning/r35-t01-science/REVIEW_R1.md`、`REVIEW_R2.md`

Refs #2092, Refs #2093, Refs #2096, Refs #2097
