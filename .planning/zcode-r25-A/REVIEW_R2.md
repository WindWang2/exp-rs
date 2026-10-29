# R25-A REVIEW_R2 — 第二轮独立复审（新上下文子代理）

- 评审者：general-purpose 子代理（新上下文，未参与实现、未参与 R1）
- 范围：R1 全部 16 项闭环验证 + R1 修复引入的新缺陷猎杀 + 并行合并风险
- 结论：**REQUEST-CHANGES**（P0×1 / P1×1 / P3×5）——P0/P1 已处置，P3 中 2 项已硬化、3 项记录留痕
- R2 同时确认：16/16 项 R1 闭环正确性逐条复核；全部 RED 声明对 master 行为核对无误（9 vs 13 日长、SID 0.0 vs inf、1000× 导数、30 vs 40 入射角、float32 16,785,408 vs 16,785,409）；Float64 产物对既有 float 读取透明；邻域 test_terrain_foundation5 / test_r24_a_terrain_flow_flats 不受影响。

## 发现与处置

| # | 严重度 | 发现 | 处置 |
|---|---|---|---|
| R2-1 | P0 | R1-2 的轴序修复只在工作区未提交：HEAD 读经度为纬度（|lon|≤90 场景静默错纬度，只有 |lon|>90 fail-closed） | ✅ commit 27a9eab316（lat=x + ±90 门 + authority-order 注释；GDAL 探针实测 45.02/116.37 证实） |
| R2-1b | P0 | 另两个修复也未提交（CRS-less fixture、derivative band 索引） | ✅ 同 commit 提交；8 个测试二进制重跑 GREEN |
| R2-2 | P1 | surface_diff_snapshot.json 未随 4 处 inputSchema 变更重生成（lat 默认移除、cloud_confidence 新增、SAR 描述变更）→ test_surface_snapshot 会红 | ✅ 构建 surface_snapshot_gen 并重生成（见 TEST_RESULTS.md 回填）；与 R25-C 的同一文件冲突风险登记 PR_COLLISION_MATRIX |
| R2-3 | P3 | 轴序硬编码：若部署设 OGR_CT_FORCE_TRADITIONAL_AXIS_ORDER=YES 会回到错读 | 部分硬化（±90 门 + 注释）；完全硬化需 SetAxisMappingStrategy 显式化——登记 RISK_REGISTER R8，不在本轨改变全局 GDAL 轴序策略 |
| R2-4 | P3 | meanScore 可为 null 而 schema 声明 number；混合情形语义变化（inf→有限均值）未在 schema 描述 | ✅ schema 输出描述补 null/infiniteScores 语义 |
| R2-5 | P3 | lookDirection=left + 默认 heading 时用户选择的 270° 不戳记（安全但丢失）；rs:sar_terrain_masks 仍无条件戳记（既有不一致） | masks 行为出本轨范围，登记 CROSS_TRACK 移交 |
| R2-6 | P3 | cellSizesMetres 无恒等 geotransform 守卫（病态输入下伪造 cellArea） | terrain_solar 已传递性保护；登记 RISK_REGISTER R9 |
| R2-7 | P3 | correction 算子 dangling-if 缩进（既有 cosmetic） | 不动（pre-existing，master 同貌） |

## R1 闭环验证矩阵（16/16 正确）
P0 定义匹配、P1-2 符号仿射中心（含旋转项）、P1-3 谓词逐字一致、P1-4..8 测试修复判别力、P2-9 真值表（4 组合全部符合设计）、P3-10..16 全部按设计落地。详见评审原文摘录于 TEST_RESULTS.md。

## master-RED 实测（内核级，独立编译）
用 master 内核（git show origin/master）编译独立 Catch2 用例：
```
MASTER-SID disjoint = 0.000000          (HEAD: +inf)
MASTER-SID classify label=0 div=0.000000 (HEAD: label=1, finite div)
MASTER-ACC corner = 16785408 exact = 16785409 delta = 1  (HEAD: exact)
```
