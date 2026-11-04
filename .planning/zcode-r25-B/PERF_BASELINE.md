# R25-B PERF_BASELINE

环境：Linux 共享宿主（兄弟轨并行构建竞争期间实跑），Release、`-j2` 构建、ctest -j1、`QT_QPA_PLATFORM=offscreen`。**墙钟数据只报告，不设硬通过门槛**（共享宿主负载抖动大）。每组配输出一致性判别器。

## 组 1 — F1 导入预检（报告必须显著轻于注册，否则 UI 逐文件预检会卡）

度量：`test_r25_b_import_preflight --durations yes`（Catch2 每用例墙钟，含 fixture 96×96 3 波段 GeoTIFF 写入）。

| 用例 | 中位 | 说明 |
|---|---|---|
| explains a plain georeferenced raster | 11 ms | 含 GDAL 打开+身份 hash+产品枚举 |
| predicts dedup against the production catalog | 3 ms | 含真实 DataManager 注册 + 只读查询 |
| 其余正例/反例 | 0–1 ms | 无 GDAL 打开的快速路径 |

一致性判别器：同源两次 `inspect` → `toJson()` 逐字节相等（测试内断言，`F1_determinism_identical` 语义）。5 次套件重跑方差：median 70.1ms，min 67.3 / max 75.2（±5.6%）。

## 组 2 — F2 推理计划（干跑必须远低于真实推理）

度量：`test_r25_b_inference_plan --durations yes`。

| 用例 | 墙钟 |
|---|---|
| metamorphic：plan vs 真实 engine.run（含 96×64 tile16=24 tiles 全推理） | ~0.03–0.05 s 段 |
| R1 收口（dtype/roles/mode/knobs/scene/multi-input 全 SECTION） | 16–31 ms |
| 质量摘要纯投影 | <1 ms |

计划本身只做元数据级 GDAL 打开（无像素读、无 session、无输出文件——测试以 `g_sessionsCreated == 0` 与输出不存在双零断言钉死），其开销落在 GDAL open 量级；真实推理的时间由同一 metamorphic 用例的 engine.run 段体现（两个数量级以上差异，只读干跑未重复推理成本）。

## 组 3 — F3/E2E

| 套件 | 5 次中位 | 方差 |
|---|---|---|
| test_r25_b_replay_deviation_v2 | 204.2 ms | 199.5–213.8（±3.5%） |
| test_r25_b_end_to_end（全链：预检→登记→计划→推理→实验→差异） | 828.3 ms | 815.7–850.2（±2.1%） |

一致性判别器：同输入 → 同 `deviation_details`/`deviations` 文本与 JSON（测试内多轮 analyze 断言）。

## 结论

无加速宣称（本轨是新增功能，无旧实现可比）；三项开销均与预期定位一致（预检/计划为解析级，E2E 由真实推理主导）。共享宿主墙钟数据 5 次重复方差 ≤6%，结论稳健。
