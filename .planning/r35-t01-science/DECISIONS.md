# R35-T01 DECISIONS（unattended 常规技术决定，按 .agents/AGENTS.md 记账）

- D-001 首切片范围 = WP01+WP02+WP03+WP04+WP05+WP06，主题"科学算法参数有效域、无信息状态与光谱有效集合"。全部为当前 HEAD 已复核的真实缺口（见 BASELINE.md 行号），各自独立可测，一个主题连贯 PR。WP07+ 留后续切片。
- D-002 WP02/WP03 helper 合同对齐内存版 `computePca`（image_enhancement.cpp:1416 `<=0 → 全部band`）：文件版在打开数据集后、任何像元扫描前把 `numComponents<=0` 归一为 bandCount；`>bandCount` 保持既有早拒绝。不选"负值报错"——与同文件内存 seam 及 rs:pca/rs:mnf operator 语义（0=all）一致，且 operator 已自行 clamp，helper 只需消除晚失败路径。
- D-003 WP04 拒绝域 = `!(trace(SXX)>0) || !(trace(SYY)>0)`（覆盖 0、负、NaN），在 ridge 之前判定，返回 false+typed 消息。秩亏但非零方差、相关band、有效同图（真实无变化）继续走既有路径——符合"不能所有相同图都拒绝"。消费方 rs_change_primitives/rs_change_streaming 均已处理 false 返回，无需改动（不改 T04 文件）。同病同修：IR-MAD 入口（change_detection.cpp 迭代内同款 trace-ridge）对常量图也会产出全 0 chi-square，加同一守卫（常量数据在任何权重下协方差为 0，任一迭代判定等价）。
- D-004 WP05 VCA：先建全 band isfinite 的有效像元索引（与 PPI 内核同谓词），均值/候选/退化门全部在有效集合上；`endmemberCount > 有效数` → false；输出仍为原始像元谱。不引入新 VCA 变体、不改变 deterministic tie。
- D-005 WP06 typed seam 统一为"恰好 requested 或 false"：PPI 分支 take<requested 时清空输出并返回 false（与 VCA 分支、参数门禁的隐含承诺一致）；该 seam 现无产品调用方（仅测试），无用户流程破坏。FCLS QA 只对全有限 abundance 像元累加；0 个有效贡献者 → violation=NaN（诚实未知，不改写 0）。不新增计数字段（YAGNI；调用方可自行数有限行）。
- D-006 WP01 修复选 stackToGeoTiff 内正则放宽为 `^(?:SR_)?B(\d+)$`（不动 `landsatBandNameFromKey` 的公开 name 语义——#613 合同声明 stack 携带 SR_Bn 名，改 name 会波及 wavelength/role/用户可见名）。ST_/QA 仍不参与反射率合同。混合 SR+ST 堆栈维持"仅 per-band stamp"。
- D-007 新测试文件 `tests/test_r35_t01_science_validity.cpp`（WP02-06 库级）与 `tests/test_r35_t01_landsat_affine.cpp`（WP01 微型 C2L2 fixture），tests/CMakeLists.txt 仅最小追加注册，不重排他轨块。
- D-008 验证目标：`test_r35_t01_science_validity`、`test_r35_t01_landsat_affine` 全 case + 直接受影响旧目标 `test_pca`、`test_change_detection`、`test_spectral_unmixing_fcls`、`test_satellite_products`。真实 case 数记 LEDGER。
