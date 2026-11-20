# R35-T01 BASELINE

- 执行时间：2026-11-18T15:54:56Z（本地 2026-11-19）
- 仓库：WindWang2/exp-rs（origin https://github.com/WindWang2/exp-rs.git）
- base：master `120677e6e165d3e88bb9d2ec2a3fd98042ec719d`（与任务快照一致）
- 执行时 open PR：0；remote branches：29；活动 PR 排他路径：无
- worktree：`/home/kevin/projects/rs-studio/exp-rs-r35-t01-science-20261118T155456Z`
- 分支：`zcode/r35-t01-science-20261118T155456Z`
- 配置：`cmake --preset dev-default`（Debug, ENABLE_TESTS=ON），configure exit 0
- 资源：双锁（host `$HOME/.cache/exp-rs-agent/host-build` + 本轨 build-dev），-j4 上限
- 指导文件：CLAUDE.md / .agents/AGENTS.md 已读（C++20/Qt6、最小改动、Catch2、QGIS 风格）

## 继承基线（不重复实现）

- #2142（SAR 物理/地形/大核 median）、#2148（BandMath 65536 预算）、#2145、#2147（谱指数 claim、变化 mask 恢复）、#2144（模型/GPU，归 T02）均已合并入 base。
- #2135（训练 CRS、feature matcher 排序等）为回归基线。
- `rs_spectral_index_operator.cpp`、`rs_change_streaming.cpp` 整文件归 T04，本轨只读。

## 首切片差距证据（当前 HEAD 静态复核，逐条源码行号）

1. WP01 `satellite_products.cpp:2014` Landsat 系数关联正则 `^B(\d+)$`；MTL 主路径 `landsatBandNameFromKey`（:114-136）对 `FILE_NAME_BAND_SR_B2` 保留 `SR_B2` 名，标准 C2L2 SR 波段不匹配 → 无 MULT/ADD stamp。fallback 目录扫描（:1094）剥 SR_ 反而能匹配。现有 #680 测试用数字键（FILE_NAME_BAND_2→"B2"），未覆盖 SR_ 键。
2. WP02 `image_enhancement.cpp:1647` processPcaFile 只拒 `>bandCount`；`<=0` 会在两遍完整扫描后于 `outDataset.create`（:1772）才失败。内存版 `computePca`（:1416-1417）合同为 `<=0 → 全部band`。operator `rs_pca_operator.cpp:163` 已自行 clamp。
3. WP03 `image_enhancement.cpp:1863` processMnfFile 同样只拒 `>bandCount`；负值迟到 `Mtrans(numComponents,...)`（:2061）抛 std::length_error；0 迟到 create（:2075）。无 src 调用方（rs_mnf_operator 自带内联实现并已 clamp :163）。
4. WP04 `change_detection.cpp:572-577` madFinalize 对 trace=0（常量图）加 `max(eps,1e-12)=1e-12` ridge 后仍 ready；H=0 的 SVD 产出 varMad=2 → 变换输出全 0"可信无变化"。无信息状态未与真实无变化区分。
5. WP05 `spectral_unmixing.cpp:601-606` VCA 对全部像元求均值，无整谱 isfinite 过滤（PPI 内核 :31 有）；任一 NaN/Inf 像元 → mean NaN → k=0 即失败，即使存在足量有效谱。
6. WP06 typed seam `extractEndmembers` PPI 分支（:584-589）可短返回（take<requested 仍 true），VCA 分支为"恰好 requested 或 false"——同一函数两种合同；typed `unmixFcls` QA（:732-745）把 NaN abundance（非有限输入像元产生）累进均值 → violation=NaN 毒化；validCount=0 与真正 0 违反不可区分。
