# R30-T06 · ACCEPTANCE（Oracle F1–F7 对照）

> 最终 HEAD 绑定；每格写 `DONE` / `ALREADY_DONE_WITH_EVIDENCE` / `BLOCKED` / `NOT_EXECUTED`。
> 状态在 R2 之后、PR 之前复核一次。

| 编号 | 必须满足的可观察结果 | 状态 | 证据 / 缺口 |
|---|---|---|---|
| F1 | 有效/空/失效组在正确活动 canvas 上缩放或诚实拒绝，无空 canvas 解引用与过期对象操作 | **NOT_EXECUTED** | C1 已 `CONFIRMED_OPEN`（`ISSUE_TRIAGE.md`）；修复与验证待完成 |
| F2 | 历史所有入口使用同一资格和身份，具体双方产物可比较；失败提交不显示 queued | **NOT_EXECUTED** | C2 已 `CONFIRMED_OPEN` 5 处子差距；修复与验证待完成 |
| F3 | 大 collection 所有记录可有界到达，选择/过滤/刷新/项目切换不串 asset | **NOT_EXECUTED** | C5 已确认（`data_manager_panel.cpp:893-937` vs `:1130-1158`）；本轮不实施，见 `HANDOFF.md` |
| F4 | 视图、图层、样式与制图输入关联明确，同源不同显示实例不被错误合并 | **NEEDS_REPRO / NOT_EXECUTED** | C4：静态去重字符串不等于证明错误，本轮不凭字符串断言 |
| F5 | 草稿/编译/Designer/检查/修复/导出指向同一明确修订，失败和旧布局状态准确 | **NOT_EXECUTED** | C3 已确认（`cartography_dock.cpp:566,496,585/613/687`）；本轮不实施 |
| F6 | 多页预览、质量问题与交付文件/manifest 可检查，内容验证不只断言文件存在 | **NOT_EXECUTED** | C6 已确认（`:531-535,723-728,766-785`）；本轮不实施 |
| F7 | 三条完整链、必要 Qt/生命周期回归和实测体验证据绑定最终 HEAD，独立 review 完成并提交 PR | **NOT_EXECUTED** | 见 `PLAN.md WP12`；本轮 PR 只覆盖部分链 |

## 本轮「完成」的严格定义

本轮 PR 只能声称 **F1 + F2 子项**的完成，且必须同时具备：

1. `tests/test_r30_t06_*.cpp` 在当前 base 上先 RED（真实失败输出），修复后 GREEN。
2. 本轨 HEAD 的独立 configure + build 成功，且 `ctest -N` / `--show-only=json-v1`
   显示新增 case 数量 > 0（不是 `_NOT_BUILT` / `_DISCOVERY_FAILED` / 0 tests）。
3. 直接受影响的既有测试（`test_r29_t04_history_actions`、`test_processing_history_model`、
   `test_r22_workbench_view_tree`）真实运行通过。
4. R1 / R2 由独立上下文完成，绑定最终 HEAD，发现全部处置。
5. `git diff --check` 干净、PR 正文列明真实命令与结果、限制与 review SHA。

任何一项缺失 → 对应 F 保持 `NOT_EXECUTED` 或 `BLOCKED`，不得写 `DONE`。
