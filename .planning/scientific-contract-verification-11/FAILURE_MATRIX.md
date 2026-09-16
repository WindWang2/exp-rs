# FAILURE_MATRIX — scientific-contract-verification-11

负路径/失败语义矩阵。每行：注入 → 期望契约行为 → 验证 → 证据。

| # | 注入 | 算子/seam | 期望行为 | 验证 | 证据 |
|---|---|---|---|---|---|
| F-1 | 伪 TIFF（prose 字节） | io:translate | typed RSOperatorError（FileNotReadable/InvalidInputData/GdalError 族），无输出文件 | test_verification_failure_11 F1 | |
| F-2 | 不存在的输入 | rs:band_ratio | FileNotFound 族 typed refusal，无输出 | F2 | |
| F-3 | 波段越界（band=99） | rs:band_ratio | OutOfRange/InvalidParameter 族，无输出（无 silent nonsense） | F3 | |
| F-4 | run 前 cancel=true | io:translate/io:warp/rs:band_ratio | typed Cancelled；若算子忽略 pre-set flag = contract finding | F4 | |
| F-5 | 只读输出目录 | io:translate | typed 写拒绝，无半成品 | F5 | |
| F-6 | NoData 孔洞钻取 | rs:band_ratio | 孔洞不复活（fail-closed 单调）；有效像素输出不变 | metamorphic M4 | |
| F-7 | 恒等 CRS 重投影 | io:warp | 像素等值（坐标不得被静默重解释） | metamorphic M5 | |
| F-8 | 乱序组合 | rs:mosaic | 并铺瓦片顺序不泄露进结果 | metamorphic M6 | |

## Pre-existing / OUT_OF_SCOPE（master 现状，含 open issues）

| 项 | 现象 | 状态 |
|---|---|---|
| #1001 | io:clip 把 srcCrsOverride 当 targetCrs（silent wrong clip） | open，OUT_OF_SCOPE（io 实现区 read-only）；F-7/M5 机制可捕获此类缺陷 |
| #1002 | workflow registry node executor 不验证 artifact 存在 | open，OUT_OF_SCOPE（workflow 区，#1009 冲突面） |
| #1003/#1004 | dataset join/qa identity fail-open | open，OUT_OF_SCOPE |
| #1005 | mapPickToLayerCrs CRS transform 异常→未变换点 | open，OUT_OF_SCOPE |
| #1006 | PipelineRunCoordinator syntheticExecute 软默认 | open，OUT_OF_SCOPE |
| #1007 | dataset:qa 从不审计 CRS | open，OUT_OF_SCOPE |
| master | test_capability_drift 2 例 pre-existing 失败（cartography knowledge + recipe summary，10.0 EVIDENCE 记录） | READINESS 中如实分类 pre-existing |

（构建/测试执行后回填"证据"列。）
