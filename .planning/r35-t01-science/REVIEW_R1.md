# R35-T01 REVIEW_R1（独立审查，绑定未提交工作树 = 最终提交前 HEAD 的内容基线）

- 审查者：独立 general-purpose agent（未参与实现，新上下文）
- 范围：`git diff`（5 个生产文件 + 2 个新测试 + tests/CMakeLists.txt 追加块 + .gitignore），关键调用方（rs_pca_operator / rs_mnf_operator / rs_change_primitives / rs_change_streaming[只读] / 相关测试），数值复核（numpy 复算 MNF 白化特征间隔 35–65%、缩放不变性误差 ≤7e-7 vs 1e-2 容差；MAD 同图对 max|chi²|≈7e-24 vs 1e-9 容差）
- 结论：**0 P0 / 0 P1 / 7 P3，无阻塞**

## 发现与处置

| # | 严重度 | 位置 | 发现 | 处置 |
|---|---|---|---|---|
| 1 | P3 | change_detection.cpp IR-MAD 守卫 | 加权重后 trace 可在构造性角落（常量背景+chi²>1490 变化像元使 CDF 权重下溢）变为 0 → 由旧的"静默用上一轮变换"变为硬失败 | 接受并已在代码注释中记录该角落为有意 fail-closed |
| 2 | P3 | rs_change_streaming.cpp（T04 文件） | IR-MAD 流式镜像无同款守卫；常量对仍经"degenerate weights"路径失败——仅错误文案不同；已核 false 传播干净（ComputationError + partial-output guard），无崩溃/静默错值 | 不改（T04 整文件）；已写入 HANDOFF.md 供 T04 决定文案对齐 |
| 3 | P3 | satellite_products.cpp 新正则 | 大小写敏感与同文件其他正则不一致（当前安全：两个 name 生产方均 toUpper） | 已修：加 CaseInsensitiveOption |
| 4 | P3 | image_enhancement.cpp 注释 | 引用了不存在的 computePca（实际是 ImageEnhancement::pca） | 已修：注释更名 |
| 5 | P3 | science_validity 测试 | REQUIRE(validPixels==5) 恒真断言 | 已修：删除该断言，改注释 |
| 6 | P3 | science_validity 测试 | FCLS 全 NaN 段在旧代码下也通过（NaN 毒化恰好也 NaN）；section 1（混合场景 isfinite）才是钉住修复的断言 | 接受：保留作为合同钉，section 1 已非空 |
| 7 | P3 | CMake 注册 | 链接了未直接使用的 Qt6::Widgets/Qt6::Test | 接受：镜像 test_pca/test_satellite_products 房风格 |

## 审查中核实的关键非问题

- trace 守卫不会误伤：秩亏非零方差/微小方差/反射率 0..1 均通过；NaN trace 不可达（累计跳过非有限像元，溢出为 +inf 通过）。
- SR_ 正则仅 Landsat L2 分支可达；S2/GF/其他产品分支不受影响；ST_/QA 不匹配；fallback 路径两种形式绑定同一系数键。
- PCA/MNF：仅有的 src 调用方（两个 operator）原本就先 clamp；基线行为确认（旧 PCA(0) 死于 0-band GDALCreate；旧 MNF(负) 死于 Mtrans length_error）。
- MAD：既有测试场景全部非常量；常量场景现在经新守卫 CHECK_FALSE 仍失败；输出 NaN 填充语义一致。
- VCA 重写：全有限输入下与基线逐操作等价（均值分母、迭代序、严格 > tie、chosen 原索引、参考像元、三个退化门）。
- PPI 短返回失败不可能误触发（内核仅在 valid<requested 时短返回；既有测试均 valid≥requested 且已断言精确数量）。
- FCLS QA：行索引、NaN 语义与 legacy 内核写 NaN 行的约定对齐；operator 侧 tile QA 本就跳过 NaN 行，一致。
- 跨轨所有权：git status 仅声明的写集被修改。
