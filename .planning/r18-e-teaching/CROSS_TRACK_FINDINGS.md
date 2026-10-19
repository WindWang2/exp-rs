# R18-E CROSS_TRACK FINDINGS

本轨发现但不越界修复的问题（PR 正文列明）：

## CT-1 (→ R18-D / src/app 维护者) P0 课程完成状态从未持久化
- 现象: `src/app/teaching/lab_cockpit_dock.cpp:371-373` 每次启动用 `CurriculumProgress::emptyDoc()` 构建课程首页; `CurriculumProgress::markCompleted` 无任何生产调用者（仅测试）→ 学生通过验证后重启, 课程首页永远不显示完成, 先修 DAG 全部不可满足。
- 本轨动作: 在 `src/teaching` 提供完成权威记录 API（T103），供 dock 替换 emptyDoc() 接线; 不改 src/app。
- 建议接线: dock 打开 lab 前读取完成账本; 验证 pass 后写入。

## CT-2 (→ R18-D) P3 run-matrix issues 不达 dock UI
- `src/app/experiment_studio/experiment_studio_dock.cpp:433-475` 只渲染 counts; `stoppedReason`/issues 断在 VM 层。建议在 matrix 表格上方渲染 issues 列表。

## CT-3 (→ i18n 维护者) P3 experiment_studio 模块不在 test_i18n.cpp 审计清单
- `test_i18n.cpp:55-68` auditedModules 未含 `src/app/experiment_studio` 等; `experiment_studio_dock.cpp:757-758,845` 有用户可见未翻译 QStringLiteral。

## CT-4 (→ #1513 继任者/store 维护者) 信息
- 原开放 PR #1513 已合并; 本轨全程未改其写集; studio 投影仅消费 StudyReport 只读接口。

## CT-5 (→ verification 维护者 / #1662 后续) master 既有红: r16a G pin 未随 pack 词表重写同步
- master e12a9afbc 重写 builtin packs 到求值器真实词表（可真实评定），但 tests/test_r16_a_verifier.cpp 的 G 用例仍 pin「builtin packs 永不 Pass」——全能 provider（其 completeness 提供 inputs 维度）下 provenance.inputs_resolved 合法 Pass → REQUIRE(Indeterminate) 红。本轨以最小跨轨修正对齐 pin（Pass 必须携带真实期望 / 否则 Indeterminate），PR 正文标注。
