# R21-E OWNERSHIP — 写区白名单（对账清单）

## 准入写区（协议 §5）
`src/teaching/**` `src/teaching_admin/**` `src/app/teaching/**` `src/app/teaching_admin/**` `src/lab/**` `src/lab_pack/**` `src/study/**` `src/experiment_studio/**` `src/grader/**` `src/verification/**` `src/verify/**` `src/verify_adapters/**` `src/faultlab/**` `src/help/**` `src/explain/**`（仅教学/证据解释相关）`packaging/**`（教学离线打包路径）`scripts/windows/**`（run_lab/grade_all 路径）+ 对应测试 + 课程示例 + `.planning/r21-E/**`。

## 预计触碰文件（随工作包更新）
- W1（verifier 生产接线）: `src/grader/grader_types.*` 或 `grader_adapters.*`（evidence 投影）、`src/cli/lab_batch_runner.*`/`src/cli` lab 命令（**注意：src/cli 不在白名单！** 若需要改 CLI 则只能通过既有 CLI 公开入口组合，或把逻辑放进 src/lab / src/grader 由 CLI 调用——见 RISK_REGISTER R3）
- W2（faultlab verifier schema）: `src/faultlab/fault_scenario.cpp` `fault_types.*` `fault_runner.cpp` + tests
- W3（help/explain P2/P3）: `src/help/help_presenter.cpp` `error_diagnostics_bridge.cpp` `terminology_provider.cpp` `src/explain/explanation_builder.cpp` `explanation_validator.*`
- W4（端到端/批量）: `tests/` 新增、`scripts/windows/*.cmd`（若需）
- 全部新增测试挂 `tests/CMakeLists.txt` append-only。

## 层级红线（来自仓库既有 guard，不得破坏）
- `sicnu_verifier` 必须保持叶子（无 Qt、无 sicnu 下游链接；verify/CMakeLists 注释明示）。
- `sicnu_explain` 禁链 sicnu_verifier/sicnu_grader（explain CMake 层 guard）。
- `sicnu_verify_adapters` 只准链 jsoncpp + sicnu_verifier（+_gdal 加 Sicnu::Geospatial）。
- grader 是叶库（零 Qt，grader_json 纪律）。teaching 保持 Qt-free leaf（tests 注释）。
- 组合层（CLI/app/tests）是 verifier 与 grader 唯一合法相遇点（evidence seam 先例）。

## 禁改（本轨只读集成）
`src/analysis/**` `src/processing/algorithms/**` `src/operators/runtime/**` `src/runtime/**` `src/data/**` `src/dataset/**` `src/experiment/**`（核心）`src/agent/**`（除教学胶水最小变更）`src/sdk/**` `src/workflow/**` `src/app/**`（白名单子目录外）`src/gui/**` `src/cli/**`（不在白名单——见 RISK_REGISTER）。
