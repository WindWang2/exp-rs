# DECISIONS.md — flash-plugin-sdk-12

## D1: 不重造已有契约，只做真实增量

源码 census 显示 WP1/WP2/WP3/WP5/WP6/WP7 的骨架都已在 #1103 及之前版本落地（三轴版本、enforcement matrix、11 态状态机、typed UI schema、package checksums、大量 hostile 测试）。本 Track 工作 = 在其契约上增量。**替代方案（自建平行 SDK）已否决**：违反 prompt「不要重新造已有系统」。

## D2: WP1 增量 = manifest 可选 min/max host API 区间

- 现状：`api_version` 单向规则（plugin.minor <= host.minor）。无法表达「此插件只在主机 API 3.0–3.2 上验证过」。
- 增量：manifest v1 **可选** `min_host_api` / `max_host_api` 字段（"MAJOR.MINOR"）。validator 在既有 gate 之后追加区间检查，失败 = E2001（ApiVersionMismatch）带 expected/actual 字段。旧 manifest 无此字段 = 旧语义不变（向后兼容）。
- 备选取舍：min/max 放在独立对象 vs 平铺字段——选平铺（manifest v1 风格，其它字段也是平铺）。

## D3: WP2 增量 = 默认模式下的结构化 grant/deny audit event

- 现状：默认策略 Audit 下 undeclared-permission 只产生 ValidationWarning；granted permission 无任何记录。Oracle 4 要求「允许权限路径有明确 audit event」。
- 增量：新增 `PluginAuditEvent` 概念落点 = 现有 PluginDiagnosticLog（Info 级，code 走新增 P2xxx 系列之一，或复用 None+message 字符串前缀）。**决定**：在 plugin_diagnostics.h append 两个 code（PermissionGranted=5011? 不——E5xxx 是 policy 组，append `PermissionGranted = 5010` 与 PermissionDenied=5001 同组，语义清晰）。registry load/unload 时对每个 manifest 声明的 permission 记一条 Info audit event（含 pluginId、permission name、policy mode、outcome）。
- host-process worker 侧：load params 已带 access 声明；worker 端 WorkDir policy 已有 PermissionDenied 拒绝；增量：worker 拒绝路径也写 audit event（已有 diagnostics 表单，补 Info grant 即可，不改拒绝路径语义）。

## D4: WP4 Hot Reload = dev-mode 专用 reload API，生产默认关闭

- 现状：无专用 API。
- 增量：`PluginRegistry::reload(pluginId, reloadOptions)` 新增：
  - 仅在 dev mode（`SICNU_PLUGIN_DEV=1`）可用，否则 typed refuse（新增诊断）。
  - 序列：drain（现有 quiesce barrier）→ unload 旧 → scan 新 manifest/validate → load 新成功 = commit；load 失败 = **回滚到旧版本**（保留旧 LoadedPlugin 直至新的成功；失败时旧版本保持 loaded 并记录诊断）。
  - state migration seam：暴露可选回调 `PluginStateMigration`（旧 state JSON → 新 state JSON），manifest 可声明 `migration` 版本对；v1 提供 identity 默认实现（空迁移，明确记录）。
  - UI schema renderer 的 detach/attach 已存在（releasePluginUi/attachPluginSchema），复用。
- 备选：在 refresh() 里隐式热更——否决（refresh 是廉价扫描路径，混入重载会破坏 startup 语义与 #928 锁协议）。

## D5: WP3 增量 = upgrade 语义

- 现状：`PluginPackage::install` 到已占用目录按 id takeover 拒绝；升级 = uninstall+install 两步，中途失败留下无插件状态。
- 增量：install 检测现有同 id 且 registry 已 loaded 时：走 upgrade 路径（drain → stage 新版本 → 校验 → load 新成功 → 卸载旧），失败保留旧版本。状态机新增 `Upgrading` 态（append-only，不动既有 enum 数值顺序语义——C++ enum class 数值仅依赖未使用处，测试按名字比对；仍按追加放最后保持既有数值稳定）。
- 简化决定：**v1 upgrade 只在 registry 已 load 时强一致**；未 load 时保持 install 覆盖行为（已有行为）。

## D6: UI/应用集成只经 seam

插件菜单/dock/命令投影已经由 shell（workbench track 拥有）经 UiShellSink + CommandRegistry 完成。本 Track 不碰 app 窗口；仅补 renderer 幂等契约测试（重复 attach/detach 不泄漏 widget）。

## D7: 构建与验证约束

- worktree `build-track/`（Debug, tests ON）；`ninja -j1`（必要时 -j2），`ctest -j1`。
- 只编受影响 target：`test_plugin_host_process`、`test_plugin_manifest`、`test_plugin_capabilities`、`test_plugin_ui_schema_host`、`exprs_plugin_host_worker`、`sicnu_plugins_hostprocess` 及相关库。
- 不在 master checkout 写码。

## D8: .planning 白名单

`.gitignore` 当前 `.planning/*` 全局忽略 + 每 track 白名单。追加本 track 白名单条目（与既有 40+ track 同模式），使 ORACLES/BASELINE/DEDUP/DECISIONS/OWNERSHIP 五份 md 可随 PR 提交。ledger（.goal-loop-ledger.md）不提交，按 prompt 要求只留 worktree。
