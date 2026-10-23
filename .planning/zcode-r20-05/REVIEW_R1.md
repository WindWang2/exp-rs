# R20-05 REVIEW_R1 — 独立评审第一轮(fresh subagent,只读,89 工具调用)

范围:`origin/master(28e1891390)...HEAD`(处置前 @813ed6ebdf+注册提交)
裁决:**REQUEST-CHANGES**(P0×1 / P2×1 / P3×6)→ 已全部处置,见文末

## 发现与处置

| # | 严重度 | 位置 | 发现 | 处置(提交 7b1d6a2778) |
|---|---|---|---|---|
| 1 | **P0** | tests/test_r15_E_batch_refusal_accounting.cpp:122-245 | 恢复的 discovery 用例与本轨(未恢复的)生产代码矛盾:master 的 discoverSubmissions(808f1c4c52)对 hidden/nested 显式维持不可见,4 用例必红;文件头"discovery 半区 master 已绿"为错误声明(评审员独立 Qt6 程序复现 0 skips) | **恢复丢失的记账块**(hidden 学生目录/hidden 文件/嵌套目录 → 类型化 DiscoverySkip 拒绝);纠正测试头与 ISSUE_TRIAGE;disc_driver 实证 base skips=0(RED)→head skips=4(GREEN) |
| 2 | P2 | src/verify_adapters/gdal_grid_probe.cpp:119-125 | census 条件按像素计数:细长栅格(200×40@axis32,9216 样本≥8000 px)短轴过采样掩盖长轴空档(列 32..84/116..168 未读)却声称 census | **改为逐轴连续性**(extent ≤ 3×window);sv2_driver 实证 elongated-stamped;细长案例钉入 test_verify_adapters_grid |
| 3 | P3 | batch_assessment.cpp loader | 垃圾类型 total(字符串/小数/负数)静默回退 rows.size() 无警告 | present-but-unusable → warning("declares a non-integer or negative total") |
| 4 | P3 | teaching_admin_dock.cpp:607 | 拒绝消息路径打印两次 | 改为单次(loader 错误已带路径) |
| 5 | P3 | .gitignore/ORACLES | 引用的 red-drivers 证据未提交 | allowlist 扩到 red-drivers/* 并全部提交 |
| 6 | P3 | src/app/teaching/lab_operator_launch.cpp:22,134;lab_cockpit_dock.cpp:323 | 同面 #1607 崩溃类站点未钉定(评审员实证 jsoncpp 1.9.8 超限抛出) | 三站点全部 pin 256 + typed catch |
| 7 | P3 | rubric_builder.h:17 | supportedProcessCriterionKinds 委托后成死 API | 删除(无调用者) |

## 评审员确认无发现(摘要)
- WP-ML 恢复忠实:与 8eded4a682(side2)逐块仅注释差异;master 侧后续修复(T102f/#1520 等)全部保留;loader 纯增量
- derive-percent 语义与恢复原版逐字节一致,分母=清单全部 lab 去重(含 optional/被先修阻断),与上游 curriculum_progress allLabs 口径一致;isfinite 处理 1e999→inf
- dock 线程模型一致(原代码也在 GUI 线程无界读;新代码有界读严格更优)
- validateProcessRubric 错误码漂移:仅两个生产消费者且均按严重度泛化处理;三个重钉套件为真 oracle(engine path/code 与 grader_types.cpp 逐一核对;NaN→null 实证)
- WP-JP 与仓库既有 pin/catch 模式一致(grader_json 256);RUN_LOG SIGABRT→refusal 真实
- 构建/范围:全部 TU 语法检查通过;两个测试注册函数存在且链接齐;sicnu_grader 不在 FORBIDDEN_LINK;无循环/无越区/无重排

## 复验
全部改动 TU g++ -fsyntax-only 通过;disc/sv2 驱动 base-RED/HEAD-GREEN(输出在 red-drivers/RUN_LOG.md)
