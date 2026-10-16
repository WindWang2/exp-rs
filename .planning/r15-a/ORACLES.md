# R15-A ORACLES — Oracle 驱动的验收定义（goal-loop 等价执行）

> 宿主未安装 `/goal-loop`（亦无 `.agents/skills` 注册），按 Prompt 规定以本文件 + `LEDGER.md` 等价执行 Oracle-Ledger 循环：**分析 → RED → 最小修复 → GREEN×2 → 记账 → 下一任务**。
> 每个 Oracle 必含：旧失败输入、准确症状（文件/断言）、新期望、退出码语义、边界行为、兼容性负面测试。
> 所有数值断言用"像元级 golden + 误差上限"，不用"感觉"。

## O1 · 融合 resample-then-mask 边缘掺混（WP-A03，来源：R14-A FINAL_REPORT 残留第 1 条）

- **旧失败输入**：pan（全色）+ MS（多光谱，某波段右边缘列声明 NoData=-9999）小合成栅格（`tests/synthetic_raster_builder.h` 有界构造，≤64×64）。
- **症状位置**：`src/processing/algorithms/image_fusion.cpp` 重采样路径（R14-A 基线行号 782-805 及 kernels 106-108/147/400-402/494/613-618 区域，BASE_SHA 上重新定位）。
- **旧行为**：R14-A 的 per-band wash 只做**精确哨兵匹配**；GDAL 重采样核在边缘把 -9999 与合法反射率**掺混**成非哨兵污染值，wash 后漏网，污染进入输出。
- **新期望**：① 边缘像元无掺混污染（与"先掩膜后重采样"参考实现在误差上限内一致）；② 合法边缘反射率不被误删（输出非空、有效边缘值与参考实现逐像元相等或容差内）；③ 输出差异有明确误差上限并写入断言。
- **退出码**：RED=Catch2 rc=42；GREEN=rc=0 且两次独立运行一致。
- **负面测试**：无 NoData 标签影像融合结果与 BASE_SHA 完全一致（不允许行为漂移）；单波段全 NoData → typed 失败而非伪造成功。

### O1 策略决策（2026-10-16 定案，依据代码证据）

1. **修复语义 = 家族部分空值惯例（Policy B）**：MS 核触及空值源的 pan 像元判空。证据：流式 brovey 的 any-void 规则（`msData[i]==nodata||isnan → validPixel=false`）与 #700"partially-NoData pixels invalidated like the kernel"、#611 的负和守卫。**不**采用"仅剔除空值源后平均"（Policy A）——那会与本家族其余部分语义不一致。
2. **修复位置 = 先洗后重采样**（native read → 哨兵洗 NaN → MEM 数据集 → GDAL bilinear，NaN 经核传播）。GDAL 的 RasterIO 重采样不识别 NoData（`readBandWindowScaled` 直通 `GRIORA_Bilinear`，无 nodata 通道），后置 wash 只能抓精确匹配。
3. **快路径保全行为逐位不变**：窗口内无任何空值（或该波段无有限声明哨兵）时走原 `readBandWindowScaled` 原样路径；MEM 子矩形映射逐行复刻 wrapper 的 clamped 映射，栅格边缘几何不变。
4. **RED 驱动哨兵选择 = -1.0（温和哨兵）**：-9999 掺混后的负和被 #611 守卫掩盖成 NaN（BASE 输出"看起来干净"）；-1 掺混值 0.5 使和为正，污染以**看似合理的错误反射率**存活（8.889/7.619 而非 6.667）——这才是诚实的 RED 症状。-9999 变体作为哨兵无关性回归锁定（BASE 已绿，防修复只对特定哨兵有效）。
5. **`src/processing/gdal/gdal_dataset_wrapper.cpp` 不在本轨可写范围**（可写范围为 `src/processing/algorithms/**`），故修复全部落在 `image_fusion.cpp` 内。
6. **已知后果（如实记录）**：fidelity-report 路径（image_fusion.cpp 约 1845 行 `readBandWindowScaled` 参考图）存在同一掺混机制，但仅影响质量报告指标而非科学输出，本轮不动（P3，记录待后续）。

## O2 · sam_classify 与 spectral_similarity 的 NoData 方言分歧（WP-A02/A13，来源：R14-A 残留第 3 条）

- **旧失败输入**：同一参考光谱 + 同一测试栅格，分别走 `rs_sam_classify_operator` 与 `rs_spectral_similarity_operator`；测试栅格含"未声明 NoData 的波段"与"含 -9999 的参考光谱"两种输入。
- **症状位置**：`src/operators/rs/rs_sam_classify_operator.cpp`（R14-A 基线 141-145）vs `rs_spectral_similarity_operator.cpp` 的 R5 per-band wash。
- **旧行为**：sam 对未声明波段走 NaN-only 政策；spectral_similarity 走 -9999 fallback；同一科学问题两条路径给不同答案且无文档/测试锁定。
- **新期望**：二选一，但必须**显式**——(a) 统一方言（合并共用判断，不破坏旧产品契约：旧 golden 全部保持），或 (b) 保留差异但补 golden 双锁 + 头文件契约注释。默认策略：(b) 优先（R14-A 明确"deliberately preserved to avoid altering existing outputs"），除非 (a) 被证明零契约破坏。
- **退出码**：RED=rc=42；GREEN=rc=0 ×2。
- **负面测试**：#1506 已合入的 heterogeneous per-band NoData 用例（test_spectral_classification）不得回归。

## O3 · #1465 "four 3-arg std::equal 越界读"定位（WP-A07/A01）

- **动作**：全仓定位 4 处 3-arg `std::equal`（`std::equal(a, b, c)` 四参数误用为三参数形式）；筛选落在 `src/analysis/**` 或 `src/processing/algorithms/**` 的站点。
- **判定**：落在本轨范围且可构造越界读 → REPRODUCED_OPEN，走 RED→GREEN；落在他人范围 → 矩阵记录 + 只读报告，不修。
- **症状**：读取越过字面量/容器末端（UB），ASan/UBSan 或静态证据。

## O4 · #1454 科学算子回调/计算副作用审计（WP-A13，只审不抢）

- **范围**：TaskCenter→rs 算子调用路径上的**回调闭包完整性**与算子计算的纯度（无隐藏写全局、无取消后继续写输出）。
- **判定**：发现科学侧缺陷 → 修算子端；缺陷在 TaskCenter 所有权文件 → 记录行号+评论，只读。

## O5 · rs_spectral_index_operator.cpp #1509 后再审计（WP-A02）

- **动作**：读 #1509 对该文件 +4/-4 diff（`gh pr diff 1509` 或 `git show`），确认 JSON 参数类型安全改动未引入数值行为变化；重跑 `test_rs_operators` 中 spectral index 相关用例作为回归基线。
- **通过条件**：BASE_SHA 上该文件相关测试全绿（基线证据），无新缺陷则记录"无问题"。

## O6+ · A01–A16 工作包审计新发现（按复现准入）

每个新发现必须提供：最小输入（合成数据，有界）、文件+行号、预期契约、旧行为证据（运行日志/断言输出）。**无复现不提交修复**（性能/静态覆盖类按 Prompt 允许以有理据证据替代 RED，但必须记录测量方法）。

## 通用证据规则

1. RED 证明方法：`git show origin/master:<file> > <file>` → 增量重建受影响目标 → 运行确认失败 → `git checkout HEAD -- <file>` → 重建。（Catch2 filter 必须先证明能匹配到用例，rc=2 视为无效证据。）
2. GREEN×2：修复后同一测试连续两次独立运行 rc=0，记录断言数。
3. 每轮记录：命令、退出码、断言数、环境（Debug/Ninja/MSVC、-j2、CTEST_PARALLEL_LEVEL=1、QT_QPA_PLATFORM=offscreen）。
4. 日志与前后代码位置存入 `.planning/r15-a/evidence/`（不提交大文件；仅 .md 结论入库）。
