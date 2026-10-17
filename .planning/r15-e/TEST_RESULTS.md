# R15-E TEST RESULTS

- 环境：Windows 10/11 x64 · MSVC 19.38.33130 · Qt 6.8.0 (msvc2022_64) · Ninja · Debug
- 构建树：`build-dev/`（本轨独立）· 编译 `-j2` · 测试 `CTEST_PARALLEL_LEVEL=1`、`QT_QPA_PLATFORM=offscreen`
- 构建命令（脚本内固定 `-j2`）：
  `cmd /c build_r15e.cmd <targets...>`
- 运行命令（PowerShell，DLL 搜索路径显式拼接）：
  `pwsh -File run_r15e.ps1 -Targets <exe...>` 等价于对每个 target
  `Start-Process .\<t>.exe -Wait -RedirectStandardOutput/Error`，记录 `EXITCODE`
- 说明：本轨未使用 CTest 驱动（`sicnu_discover_tests` 已注册，但逐个直接执行可给出确定性的 exit code 与断言数），
  因此**不声称**「CTest 全绿」，只报**直接运行测试二进制**的结果。

## 1. 新增测试（RED → GREEN）

### 1.1 `test_r15_E_lab_pack_bounds`（E09 · lab pack 解析边界）

```
第一次运行（修复前）  EXITCODE=42
  test cases: 3 | 2 passed | 1 failed
  assertions: 7 | 6 passed | 1 failed
  FAILED: tests/test_r15_E_lab_pack_bounds.cpp(48)
    due to unexpected exception with message:
      Exceeded stackLimit in readValue().
```
→ **RED 实锤**：超深嵌套 JSON 让 `Json::Exception` 从 `PackVerifier::loadFromBytes()` 逃逸。

```
修复后  EXITCODE=0
  All tests passed (8 assertions in 3 test cases)
```
用例：超限字节数被拒（errorCode == `lab.pack_schema`，message 含 "too large"）；超深嵌套被拒（typed）；小而不合 schema 的文档仍走常规拒绝（不被 size 错误掩盖）。

### 1.2 `test_r15_E_result_artifact_honesty`（E05 · 产物与状态诚实性）

```
EXITCODE=0   All tests passed (23 assertions in 3 test cases)
```
用例：存在产物 enabled + 双击发信号且路径正确；**缺失产物 disabled、无 UserRole、双击不发信号**；`status="failed"` 时状态行为 `✗` 且 `state=="error"`；`status="completed"` 时仍为 `✓`/`ok`。

### 1.3 `test_r15_E_teaching_stats_honesty`（E08 · 教学统计与反馈真实性）

```
EXITCODE=0   All tests passed (23 assertions in 4 test cases)
```
用例：`overall_percent=105` → 夹取到 100；陈旧 `0` 且 2/2 完成 → 100；1/2 完成 → 50；缺布尔 `passed` 的 evidence → `indeterminate`/「不确定」/`countsAsPass==false` 且整体**不是** `fail`；显式 `passed=false` 仍为 `fail` 且整体 `fail`。

### 1.4 `test_r15_E_batch_refusal_accounting`（E08/E09 · 拒绝可观测性）

本文件在**修复后**首次编译运行；RED 证据为静态可判定（裸 `continue` 与无 parse 检查）并以 case 形式固化为断言。
结果见第 3 节（本轮构建运行记录）。

### 1.5 `test_r15_E_task_panel_state`（E05 · 面板状态机）

用例：`showResult` 后结果卡可见 → `setFailed` 后不可见；`setRunning(true)` 后不可见；`formatProgress(Failed/Canceled) == "—"`，`Running/Completed` 保留百分比，负值 `…`，越界夹取 `100%`。
结果见第 3 节。

## 2. 既有测试改动（诚实说明）

`tests/test_rs_result_summary.cpp` 首个用例原先写入 `"/tmp/fused.tif"`（一个**不存在**的路径）并断言「双击它必须发出 `openPathRequested`」。
这条断言恰好固化了本 PR 要修的缺陷（缺失产物必须惰性化），因此把它改为在 `QTemporaryDir` 中写一个**真实文件**再断言，
并在文件头注释里写明原因。**这不是削弱测试**：缺失产物的行为由新增的
`test_r15_E_result_artifact_honesty` 以更强的断言（disabled + 无 UserRole + 不发信号）覆盖。
改动后 `test_rs_result_summary` 仍是 3 cases / 29 assertions 全绿。

## 3. 本轮最终构建 / 运行记录

见下方「最终记录」小节（定稿时补入本轮的完整命令、exit code 与断言数）。

## 4. 回归清单与结果

见下方「最终记录」小节。
