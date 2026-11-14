# R30-T06 合并前运行验证（2026-11-14）

本记录覆盖原 Draft 时 F1/F2 的构建与运行阻塞。验证生产/测试提交为 `87427610ddd08e85e016dd0bbce4b2e847a23fe9`，包含最新 master `affbd228d52fcc51018cc12dd32b04f81a709cff`；其后提交只更新交付文档。

## 构建与测试

Windows、MSVC、Qt 6.8、Ninja；所有构建使用 resource_guard 与 `--parallel 2`，测试使用 Qt offscreen、Qt/vcpkg DLL 路径与 PROJ/GDAL data 目录。

构建命令（下方 targets 均为真实 CMake target）：

```powershell
python scripts/dev/resource_guard.py --lock-dir build-dev -- cmake --build build-dev --target test_r30_t06_group_zoom test_r30_t06_history_action_parity test_r29_t04_history_actions test_processing_history_model test_r22_workbench_view_tree test_execution_fingerprint test_science_context_live_authorities --parallel 2
```

CMake configure/generate 与上述 7 目标构建均成功。主 checkout 复用已有 build-dev；运行源码已切换到绑定提交，未使用其他分支旧源码冒充验证。

定向 CTest driver 只 include 这 7 个目标原本生成的 `build-dev/tests/<target>-*_include.cmake`，未替换命令、测试过滤条件、断言或超时。这样排除了其他未构建目标的 PRE_TEST discovery 干扰。可按以下方式重建 driver：

```powershell
$targets = @('test_r30_t06_group_zoom','test_r30_t06_history_action_parity','test_r29_t04_history_actions','test_processing_history_model','test_r22_workbench_view_tree','test_execution_fingerprint','test_science_context_live_authorities')
$driver = Join-Path (Resolve-Path build-dev) 'merge-prs-20261114-ctest'
New-Item -ItemType Directory -Force -Path $driver | Out-Null
$lines = foreach ($targetName in $targets) {
    $registration = @(Get-ChildItem -LiteralPath build-dev/tests -Filter "$targetName-*_include.cmake")
    if ($registration.Count -ne 1) { throw "Expected one registration for $targetName" }
    'include("' + $registration[0].FullName.Replace('\','/') + '")'
}
[System.IO.File]::WriteAllLines((Join-Path $driver 'CTestTestfile.cmake'), $lines, [System.Text.UTF8Encoding]::new($false))
ctest --test-dir $driver -N --show-only=json-v1
ctest --test-dir $driver -j1 --output-on-failure
```

`ctest -N --show-only=json-v1` 实际发现 58 个用例，分布如下：

| 目标 | 实际用例数 | 结果 |
|---|---:|---|
| test_r30_t06_group_zoom | 5 | PASS |
| test_r30_t06_history_action_parity | 8 | PASS |
| test_r29_t04_history_actions | 5 | PASS |
| test_processing_history_model | 5 | PASS |
| test_r22_workbench_view_tree | 2 | PASS |
| test_execution_fingerprint | 17 | PASS |
| test_science_context_live_authorities | 16 | PASS |

新增前缀分别为 `test_r30_t06_group::` 5 个、`test_r30_t06_history::` 8 个。最终输出：

```text
100% tests passed, 0 tests failed out of 58
Total Test time (real) = 34.07 sec
```

R1 P1-4 的旧 retry/resubmit 文案回归已包含在 R29 history 5 个用例中实际通过；失败提交仍保留 `No task was queued.`，未恢复“新任务 -1”的虚假成功。

## 真实发现与最小修复

- master 的静态局部缓存被 lambda 显式捕获，Clang 不接受；改为 captureless lambda，保留一次环境初始化语义。
- live-authority 测试调用 StateAssetFactsProvider，却未链接实际实现；补 `Sicnu::PreflightStateAdapter`，构建和 16 个运行用例均通过。
- 新旧历史夹具误把插入顺序当模型顺序；固定时间、按 identity 找目标，并明确断言当前任务不同于首个选中任务。比较夹具仍验证两侧第二产物的实际信号参数。
- 首次 58 用例运行：54 PASS、4 FAIL。其中 3 个失败揭示真实 UI 缺口：只改变 currentIndex、不改变选中集合时，按钮没有重新计算资格，而菜单/ActionPlan 已变化。补 `currentChanged -> updateActionStates`，未改资格规则、未放宽断言，最终三个用例通过。
- 第 4 个失败为 Windows 参数编码：LRU 用例名的 em dash 导致 Catch 无匹配用例，测试体没有执行。仅改为 ASCII hyphen；最终实际运行并通过全部 LRU 断言，未改缓存实现。
- 独立子 agent 复核构建修复、夹具及 currentChanged 补丁，无具体阻断项；未替代上述真实运行。

## 集成完整性

```text
python scripts/check_merge_registration_integrity.py --base origin/master --all --require-refs
integrity: 1204 registered targets + 56 add_test names at HEAD (base origin/master: 1202/56; lost: 0+0; added: 2+0)
integrity: 0 marker finding(s); 0 duplicate(s); mode=git
```

`git diff --check` 通过。上述结果是本地定向构建/测试，不声称全仓所有 target 或 GitHub CI 已通过。

## 保留范围

本 PR 交付 F1/F2。原文档所列其他 WP、三条完整交付链、真实桌面高 DPI/焦点/主题/屏幕阅读验证仍 NOT_EXECUTED；offscreen 不证明这些体验。未补做 base 的 RED 运行；已有调用链证明与相关限制继续保留。

