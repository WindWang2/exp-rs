# ORACLES.md — flash-plugin-sdk-12

五个客观 Oracle，与 Track prompt 一一对应。每条含验证命令与判定标准。

## O1 — 版本协商：兼容成功 / 不兼容 typed reject

- 判定：旧版兼容 fixture（api_version minor ≤ host）协商成功进入 Loaded；不兼容（major 不同或 minor > host，ABI 不匹配）在执行前被 typed reject，且 reject 携带 E2001/E2002/E2003 + expected/actual 结构化字段。
- 命令：
  ```
  ctest --test-dir build-track -j1 --output-on-failure -R "test_plugin_manifest|test_plugin_capabilities|test_plugin_host_process"
  ```
- 新增测试：`negotiation min/max range` + `incompatible never executes`（fixture 声明 min/max host api 区间，落在区间外 = typed reject 且在 valid）。

## O2 — 生命周期资源回收：重复 cycle 后回基线

- 判定：C 轮（默认 5）load→unload 与 load→crash→restart→unload 后，worker 进程数、dlopen 句柄（LoadedPlugin 记录数）、UI 渲染记录数、restart counter 均回到 cycle 前基线；无 descriptor/线程泄漏。
- 命令：
  ```
  QT_QPA_PLATFORM=offscreen build-track/test_plugin_host_process.exe "[baseline],[recycle]"
  ```

## O3 — hostile worker 输入不 crash host + 上限

- 判定：wrong types / deep schema / oversized message / pipe descendants / worker crash / restart exhaustion / concurrent close 全部返回 typed envelope；frame cap、group depth、control count 上限生效；host 进程不 abort。
- 命令：`QT_QPA_PLATFORM=offscreen build-track/test_plugin_host_process.exe` 全量 + `test_plugin_ui_schema_host` 全量。

## O4 — 权限：拒绝无副作用；允许有 audit event

- 判定：Audit 与 Enforce 两种模式下，denied permission 产生 PermissionDenied 诊断且插件不 Loaded；granted permission 产生结构化 audit event（Info 级，含 pluginId + permission + mode）。默认策略（Audit）也记录 grant audit。
- 命令：`build-track/test_plugin_manifest.exe "[permissions]"` + 新增 audit-event 断言。

## O5 — 测试两次通过（Windows）

- 判定：以下两个 lane 完整连续通过两次：
  ```
  ctest --test-dir build-track -j1 --output-on-failure -R "plugin"
  QT_QPA_PLATFORM=offscreen build-track/test_plugin_host_process.exe
  ```
- POSIX 可运行部分在本地不可执行（Windows host）：在 PR 中标注，代码保持 QT_QPA_PLATFORM/`#ifdef` 可移植（不引入 Windows-only API）。

## 完成条件汇总

[ ] O1 [ ] O2 [ ] O3 [ ] O4 [ ] O5(×2) + review P0/P1=0 + PR created。
