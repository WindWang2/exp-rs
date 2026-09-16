# TEST_MATRIX — spectral-intelligence-11

每项能力 → 独立 oracle → 命令 → exit → evidence。

命令：`cmd //c sic11-test.cmd <targets>`（vcvars64 + qca/kc/Qt DLL 路径 + QT_QPA_PLATFORM=offscreen）。
双验证 = 2026-09-16 Phase 8 连续两遍（Oracle #6）。

| 能力 | 独立 oracle | Test target | Pass1 | Pass2 |
|---|---|---|---|---|
| A 双窗 RX vs 独立窗口枚举参考（Full+Diagonal，内部/边界/角点） | 测试内独立实现（均值/N−1协方差/loading/Gauss-Jordan） | test_spectral_local_rx | exit 0（49 断言） | exit 0 |
| A 注入异常支配 + guard 规范性质（异常自身分数 guard3 > guard1） | 解析方向断言 | 同上 | exit 0 | exit 0 |
| A NoData 排除 / minSamples 诚实 unscored / 校验拒绝 | 计数面 + NaN 语义 | 同上 | exit 0 | exit 0 |
| B identity 字典软阈值闭式 max(x−λ,0) | 逐元素闭式 | test_spectral_sparse_unmixing | exit 0（56 断言） | exit 0 |
| B 正交字典逐原子闭式 / 纯像素 / 超完备支持稀疏性 | 闭式 | 同上 | exit 0 | exit 0 |
| B 病态拒绝（共线/近共线/零范数/NaN/2048 上限）/ 非收敛诚实 | typed refusal | 同上 | exit 0 | exit 0 |
| C 闭式对：SAM=arccos(4/5)、SID=(2/3)ln2、两形态 hybrid | 手算闭式 | test_spectral_hybrid_similarity | exit 0（57 断言） | exit 0 |
| C 尺度不变 / 正交界定（Product=0，ClassicTan=+inf）/ nodata 不可评分 / grid 守卫拒绝 | 性质断言 | 同上 | exit 0 | exit 0 |
| C 分类 argmax + 形式解析 fail-closed | 构造谱 | 同上 | exit 0 | exit 0 |
| D 角矩阵对称/对角0 vs 独立计算；零范数拒绝 | 独立 dot 计算 | test_endmember_analysis | exit 0（62 断言） | exit 0 |
| D 聚类合并手算簇 + PPI 代表 + 阈值0直通 + 退化拒绝 | 手工簇枚举 | 同上 | exit 0 | exit 0 |
| D 线性插值精确值 1.5/2.5；常谱高斯不变；覆盖 flag/requireFull | 闭式 + 不变量 | 同上 | exit 0 | exit 0 |
| E 链路 PPI→analysis→sparse：provenance/license/digest 逐跳守恒 | registry 级工件断言 | test_spectral_pipeline_11 | exit 0（4853 断言） | exit 0 |
| E local RX 算子：异常恢复/质量面/评分诚实计数；hybrid 分类 | 栅格回读断言 | 同上 | exit 0 | exit 0 |
| F workbench 面板：载入/选择联动/clamp/坏工件拒绝 | offscreen widget 断言 | test_spectral_workbench_panel | exit 0（20 断言） | exit 0 |
| H 1024 band 闭式 + determinism（memcmp bit-identical）+ 8193 拒绝 + 1024×256 字典 | 闭式 + memcmp | test_spectral_scale | exit 0（108 断言） | exit 0 |
| G capability drift（43 sidecars byte-exact） | generateCatalog 对 disk | test_algorithm_meta_drift | exit 0（4566 断言） | exit 0 |

## Pre-existing（baseline 即红，非本 track 域；#1008 亦独立记录同一画像）

| Gate | 失败域 | 证据 |
|---|---|---|
| test_capability_drift | preprocess.json 重复条目（gaofen/zy3/hj_import）；cartography:* 未覆盖；io:* 未覆盖；workflow verdict/recipe | log 失败列表无本 track 条目（我的 4 算子已覆盖） |
| test_capability_knowledge | catalog 125 vs registry 136 偏斜；rs:ace capability block drift；pi/knowledge 页面 drift | 失败 id 均为 master 既有算子 |
| test_drift_projection_10 | 21 断言失败（同 catalog 偏斜域） | 同上 |
