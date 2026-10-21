# R19-01 REVIEW_R1 — 第一轮独立评审（Reviewer 1）

- 评审者角色：独立 general-purpose 子代理（非实现者，未参与任何编码）
- 审查对象：40674def9 / d040e9325 / 17b24d394（基线 e12a9afbc）
- 方式：逐提交逆向 diff + 全上下文读码 + 调用方/注册表/fixture 全量核对（58 次工具调用）
- 结论：**REQUEST-CHANGES**（1×P1、2×P2、4×P3）

## 发现清单与处置
| 级别 | 位置 | 问题 | 处置（本轮）|
|---|---|---|---|
| P1 | rs_operator.cpp execute() | 区间校验只挂在 execute()，生产主路径（atomic_algorithm_adapter:362、job_engine:1099、pybind、worker mains、batch dialog）全部直调 run()，72 处声明在生产路径零强制 | **已修**：validateParams 提为 RSOperator 公有 API；接入 adapter 与 job engine 两个中心漏斗（无开放 PR 竞争）；pybind/batch dialog/worker mains 属他轨写集 → CROSS_TRACK_FINDINGS X1 扩展记录 |
| P2 | format_profiles.h:86 | 头文件契约注释仍是 "first match" | 已修：改为 longest dot-anchored suffix + 先注册平局语义 |
| P2 | cn_product_metadata.cpp | stat-then-slurp 对 FIFO/特殊文件（file_size 返回 0）与 TOCTOU 不设防 | 已修：改为真正分块有界读，超限即在读中拒绝（内存上界 bound+64KiB）|
| P3 | rs_operator.cpp | minimum/maximum 非 numeric 时 asDouble 可能抛 Json::LogicError 逃逸 | 已修：非 numeric 声明跳过校验 |
| P3 | rs_operator.cpp | schema() 每次重建 + schema() 抛异常的 operator | 保留现状（execute 为冷路径；评审确认无热路径影响）|
| P3 | format_profiles.cpp | 裸 "zarr.json"（无前置点）仍 → GeoJSON | 保留（与旧行为一致的锚定语义），已写入头文件契约注释 + 测试钉住 |
| P3 | test_io_paths.cpp | 缺负向钉子 | 已修：`.tar.gz`→nullptr、无扩展→nullptr、裸 `zarr.json`→GeoJSON 三钉 |

## 评审确认无问题的项（摘）
- 最长后缀算法边界（at-1 不下溢、空扩展、大写、percent-decode/query-strip 顺序、隐藏文件）全量核对通过；全注册表唯一多段声明是 zarr.json，行为 delta 精确限定为 `*.zarr.json`: GeoJSON→Zarr
- 三个 profileForPath 调用方（probe/doctor/capabilities）无破坏
- 全部 73 处 setRange 默认值都在声明区间内（无"传文档默认值反被拒"不对称）；otb 组内建 bounded 取值器与新门语义一致（同为抛 InvalidParameter）
- 4MiB 上限不误伤（data/ 无大 XML fixture；CN 测试 fixture 全为 KB 级内联）
- 三个新测试均为真 RED→GREEN，无恒真/伪绿

## 复测要求
修复后重跑：T1.1/T1.2/T1.3 GREEN×2 + 三二进制全量 + adapter/job_engine 相关既有测试（test_atomic_algorithm_adapter、test_job_engine）
