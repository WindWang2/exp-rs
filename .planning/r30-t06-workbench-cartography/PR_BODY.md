组内含空间图层时，“Zoom to Group”可能解引用空 canvas，或在多视图下作用到错误地图；历史操作按钮、菜单与执行入口的资格也不一致，多产物比较固定取第一个文件。

本 PR：
- 组缩放使用当前活动 canvas，重新检查对象存活与 CRS/extent，并对无效组明确拒绝。
- 历史操作共用 ActionPlan 和任务身份，菜单、按钮及执行入口保持一致；仅切换当前行时也刷新按钮。
- 比较时分别选择两侧真实栅格产物，重提交仅在取得正的新任务 ID 后显示 queued。
- 合并验证补齐两处既有构建问题：静态缓存一次初始化的可移植捕获方式，以及 live-authority 测试缺失的 PreflightStateAdapter 链接。历史夹具固定排序并保留跨任务身份断言；缓存测试名称改为 ASCII 以避免 Windows 参数编码不匹配。

验证绑定生产/测试提交 `87427610ddd08e85e016dd0bbce4b2e847a23fe9`，之后仅更新交付文档：
- Windows / MSVC / Qt 6.8，resource_guard + CMake 构建 7 个目标成功，`--parallel 2`。
- 使用这 7 个目标的原生 CMake/Catch2 注册做定向 CTest 发现：58 个真实用例；新增 group/history 前缀分别为 5/8。
- `ctest -j1 --output-on-failure`：58/58 通过，0 失败，34.07 秒。含 R29 history 5、history model 5、R22 workbench 2、execution fingerprint 17、live authorities 16；旧 retry 文案兼容性已实跑。
- 合并注册完整性检查通过，无测试目标丢失、重复声明或残留冲突标记；`git diff --check` 通过。

详细命令、失败→修复→通过的证据及范围见 [MERGE_VALIDATION_20261114.md](.planning/r30-t06-workbench-cartography/MERGE_VALIDATION_20261114.md)。

本次范围为 F1/F2；其他工作包与三条完整交付链仍未执行。Qt offscreen 不证明高 DPI、主题、焦点和屏幕阅读体验；未补做 base 上的 RED 运行。GitHub CI 状态与上述本地定向验证分别记录，不宣称全仓 CI 已绿。

Refs #1934
Refs #1805
