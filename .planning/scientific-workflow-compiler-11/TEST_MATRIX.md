# TEST_MATRIX — 能力 → 独立 oracle → 命令 → exit → evidence

Oracle 独立性原则：时间/日期期望值全部手算（UTC epoch 常量）；cadence/分辨率类别用闭表字面期望；
mask 词表用 producer 端 round-trip 交叉钉死；determinism 用重复调用字节相等；不使用被测实现产 oracle。

| 能力 | oracle | 命令 | exit | evidence |
|---|---|---|---|---|
| 时间解析（date-only/Z/naive/±offset/分数秒/垃圾） | 手算 epoch 常量（1704067200 基准） | ctest -R test_workflow_facts_11 | (pending build) | |
| cadence regularity/label | 手算 gap（16d regular；闰年 monthly；1/59/1d irregular） | 同上 | | |
| 截断/dedup/unparseable 诚实 | kMaxDates 边界 + 字面断言 | 同上 | | |
| 分辨率类别（ metre 阈值 10/30；degree→unknown_meters） | WKT 样本 + 字面类别 | 同上 | | |
| mask 词表 producer/harness 一致 | datasetUnderstandingFromRasterInspect round-trip | 同上 | | |
| product level 解析（L2A/Level-1C/2A） | 字面 (gen,suffix) | 同上 | | |
| model task 规范化 | 字面 family 映射 | 同上 | | |
| resource budget 双侧已知才判定 | 字面 overBudget/unknown | 同上 | | |
| facts digest 确定性 | 重复调用相等 + 16 hex | 同上 | | |
| temporal_calendar 检查（range/regular/cadence/skip） | 字面日期 + 四态 ledger | ctest -R test_workflow_analysis_11 | | |
| numeric_domain_chain（dn/db error；toa warn；同域 skip） | 闭表字面 | 同上 | | |
| band_identity（重复源 warn；异源 pass） | IR wiring 字面 | 同上 | | |
| output_identity（kind 冲突 error；重复声明 warn） | 字面 | 同上 | | |
| probe scope 校验先于 I/O | 未知 scope → INVALID_PARAMETER | ctest -R test_grounding_probes_11 | | |
| probe 反幻觉（未解析 ref → DATASET_NOT_FOUND） | 字面码 | 同上 | | |
| probe 缓存复用（live→cache 同事实） | source 字段 + 事实相等 | 同上 | | |
| probe 超时诚实（elapsed/deadline_exceeded） | 字面字段 | 同上 | | |
| model probe（未知 MODEL_NOT_READY；artifact stat） | ledger 注入 + 不存在路径 | 同上 | | |
| projection 确定性/附加不覆盖/回滚保留 | 字节相等 + digest | ctest -R test_provenance_projection_11 | | |
| compile sidecar atomic（成功 + 失败 typed） | 真实文件 + 不存在目录 | 同上 | | |
| prepared decision 排序确定性/拒绝永不自动 | risk/cost 字面序 | 同上 | | |
| explain zh 回溯/有界/截断诚实 | 字面 zh 子串 + 上限断言 | 同上 | | |
| compile_workflow E2E 携带 metadata.compiler | 解析 workflow_json + digest 16 hex | 同上 | | |
| eval corpus（含新增 2 case + probe case） | runner schema/断言 | ctest -R test_harness_eval_corpus | | |
| bridge compiler surface（静态 3 + 行为 2 canary-skip） | 跨语言锚 + fake server | node --test pi/test/scientific_workflow_compiler_11.test.mjs | 0 | 3 pass/2 skip(host) |
