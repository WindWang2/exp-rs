# ISSUES — D3 lab content expansion track

本文件记录 D3（时序 / SAR / 高光谱 / 制图出图）内容扩展过程中发现的**平台算子缺口**。
本 track 承诺不修改 `src/operators/`（track GOAL 硬约束），缺口在此登记，供平台 track 排期。
每条注明影响的教学场景与建议方向；编号连续，便于判分意图 / LabSpec 回溯引用。

## 算子缺口

### T-1 `rs:temporal_monitor` 只接受 workspace collection，不接受 scenes 路径数组
- 现象：所有其他 `rs:temporal_*` 都能用 `scenes:[...]` 直接消费文件列表；monitor 的 schema 要求
  `collection`（workspace UUID / descriptor 路径），headless 管道若没有 workspace 上下文无法使用。
- 影响：实验8（时序）无法引入 CUSUM/EWMA 连续监测教学环节。
- 位置：`src/operators/rs/rs_temporal_monitor_operator.cpp`（schema required `{collection, output, method}`）。

### T-2 `rs:temporal_gap_fill` 不能生成规则日历重采样
- 现象：只能在既有获取日之间插值；没有"按 16 天/月规则日历输出"模式。
- 影响：实验8 只能以"插值补洞"教学，不能演示"重采样到统一观测网"。

### T-3 无联合"趋势+季节"分段模型（CCDC/BFAST 类）
- 现象：`rs:temporal_harmonic_fit` 对全序列拟合单组谐波；`rs:temporal_breakpoints` 提供分段斜率但无逐段谐波。
- 影响：实验8 思考题 2 只能口头讨论，无法上机验证"季节+突变联合建模"。

### S-1 `rs:sar_change` 严格双时相；`rs:sar_temporal_stats` 的 argmax_date 是场景索引
- 现象：多时相 SAR 变化只能给 0-based 场景索引（`src/processing/algorithms/sar/sar_temporal.h:42`），
  没有带日历日期的变化图。
- 影响：实验9（SAR）"多时相变化检测"只能作为诚实范围注记。

### S-2 无 SAR 极化分解（H/A/α、Freeman-Durden、Cloude-Pottier）
- 现象：只有 `rs:sar_dualpol_features` 双极化特征栅格。
- 影响：实验9 不含极化教学；全极化实验课无法开设。

### H-1 `rs:sam_classify` / `rs:spectral_unmixing` 无 `libraryPath` 参数
- 现象：refs/endmembers 只能内联 JSON 数组；`sicnu-spectral-library` 文件（ADR 0081）需手工拆包。
- 影响：实验10 用"手工复制 + 零漂移判分（H6）"兜底；学生改库后管道不同步的风险高。

### H-2 `rs:endmember_extraction` 结果只有 JSON，不能被后续步骤管道内消费
- 现象：输出 endmembers/indices/ppiCounts 为 JSON 结果对象；占位符语法
  （`src/workflow/placeholder_grammar.h`）只解析文件路径字符串，无法把数组传入下一步 refs。
- 影响：实验10 的 PPI→SAM 串联必须手工内联；PPI 只能作为"演示 + 对照判分"。

### H-3 无 MNF 反投影（inverse MNF）算子
- 现象：MNF 正变换输出分量栅格，但无逆变换；MNF 空间提取的端元无法变回反射率空间做 SAM/SID。
- 影响：实验10 统一在反射率空间跑 PPI/SAM/SID/解混（科学上正确但偏离"经典 MNF→PPI 流程"教学叙事）。

### C-1 制图 compose/validate/preflight/export 不是管道算子
- 现象：`cartography:*` 是代理工具（GUI 二进制 `--mcp` 模式）；`sicnu_geo_rs_cli --pipeline`
  无法声明"地图排版/导出"步骤，`rs_operators_init.cpp` 中无 `rs:export`/map 算子。
- 影响：实验11 的 headless 证据分两层（数据链走 runner 管道；排版导出走 lab 链测试与 MCP 脚本）。
  若成为管道算子（如 `rs:mapspec_compose` / `rs:map_export`），四层实验链即可一管道到底。

### C-2 `rs:temporal_extract_series` 只支持单点/单面
- 现象：一次调用提取一个点或一个多边形；多地块 zonal 时序需循环调用。
- 影响：实验8 的多地块物候对比表格需学生重复操作或脚本循环。

## 已修复缺陷的教学利用（非缺口，记录为回归锚点）

- **#785 视角方位角**：`rs:sar_terrain_correction` 的 `headingDeg + lookDirection` 组合推导、
  显式 `lookAzimuthDeg` 覆盖、"UI 不得自动填"约定 —— 实验9 步骤 9.5 + 判分 S5 作为回归性教学断言。
- **#803 多波段 NoData**：`rs:sar_speckle` 逐波段 NoData 哨兵解析 —— 实验9 步骤 9.4 + 判分 S4。
