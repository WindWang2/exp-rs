# R35-T01 PLAN

## 切片1（进行中）：WP01–WP06 科学有效域与诚实状态

| WP | 状态 | 说明 |
|---|---|---|
| WP01 Landsat SR_Bn 系数关联 | 实现完成，待验证 | `^(?:SR_)?B(\d+)$`；测试 test_r35_t01_landsat_affine |
| WP02 PCA helper 早归一 | 实现完成，待验证 | `<=0→all`，与 computePca/operator 对齐 |
| WP03 MNF helper 早归一 | 实现完成，待验证 | 同上；另加缩放不变性数值 oracle |
| WP04 MAD/IR-MAD 无信息状态 | 实现完成，待验证 | trace<=0/NaN 拒绝；真实无变化保留 |
| WP05 VCA 有效谱集合 | 实现完成，待验证 | isfinite 预过滤+原索引映射 |
| WP06 PPI seam 合同 + FCLS QA | 实现完成，待验证 | 恰好-requested；QA 有限行分母 |

## 后续切片候选（未开工，验证差距后再定）

- WP07 spectral reference target/interference 来源独立（#2101）
- WP08 measured/synthetic/license/citation 贯穿（#2105）
- WP09–WP14 NoData/dtype 诚实输出族（#2103 子项）
- WP15–WP16 训练提取预算与两遍合同
- WP17 分类 backend 输出矩阵合同
- WP18 sidecar/evaluation typed 恢复
- WP19–WP20 全局 CC/sieve 有界执行

## 验证链（本切片）

- E2E-02 部分：PCA/MNF 参数域+端元/解混 seam 合同（新测试覆盖）
- E2E-01 部分：Landsat C2L2 SR stack affine stamp（新测试覆盖）
- 受影响旧回归：test_pca / test_change_detection / test_spectral_unmixing_fcls / test_satellite_products
