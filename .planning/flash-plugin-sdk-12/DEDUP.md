# DEDUP.md — flash-plugin-sdk-12

实时状态（2026-09-20）：open PR = 0、open issue = 0；origin/master = adf8f9895。

## 已存在能力判定（源码级 census 结论 — 不重造）

| 工作包 | 已有 | 判定 | 增量点 |
|---|---|---|---|
| WP1 版本协商 | 三轴版本 model (src/sdk/exprs/version.h:39-88)：EXP_RS_PLUGIN_API_VERSION 3.0 / ABI 1 / MANIFEST 1；host_protocol.h 四轴 (protocol 1.2)。plugin_validator.cpp:195-243 E2001/E2002/E2003 typed reject (same major, declared.minor<=host.minor)。host_process 侧 handshake 协商 (plugin_host_worker_main.cpp:980-1010 limits/features)。test_plugin_manifest.cpp:487 "api compatibility rule"。 | **大部分已有** | min/max 区间协商 + 协商成功的 fixture 契约测试；insufficient 的 "expected/actual" 机器可读字段 |
| WP2 权限 | plugin_permissions.h (7 权限枚举 + Audit/Enforce policy + env)，plugin_capabilities.h (access 声明 + enforcement matrix + 诚实边界文档)，registry Enforce 拒绝 (plugin_registry.cpp:277-301)，worker-side workDir policy (plugin_host_worker_main.cpp:566)，model framework gate / ui:false gate / dataProvider scheme gate。 | **大部分已有** | **Audit 模式（默认）下没有 grant/deny 的结构化 audit event**——Oracle "允许权限路径有明确 audit event" 未满足。host-process permission grant 传播到 worker 的 seam 未接线 |
| WP3 生命周期 | 11 态状态机 (plugin_record.h:16-31)，Quiescing + drain barrier (plugin_loader.h:49-69)，unload timeout/refuse (plugin_registry.cpp:538)。unload round-trip 测试存在。 | **大部分已有** | **upgrade 语义缺失**（无 Upgrading 态；install 覆盖旧版本 = uninstall+install 两步非原子）；restart-exhaustion 后的 worker/句柄计数回基线无循环 Oracle |
| WP4 Hot Reload | 无专用 API。refresh() 保留 loaded records；unload()+load() 可手工组合。 | **完全缺失** | dev-mode 专用 reload：安全重载 + 失败回滚 + state migration seam + 生产默认关闭 |
| WP5 UI Contract 2 | typed schema (plugin_ui_schema.h/.cpp), host-owned widgets (plugin_ui_schema_host.cpp), state patch, bounded queue (64), #1039/#1040 fixed, E6010 host validation。Ribbon/CommandRegistry 投影由 shell 负责（workbench track seam）。 | **已有** | 仅补增量：dock/menu ownership 重复 attach 的幂等契约测试 |
| WP6 包完整性 | plugin_package.cpp:270-460 checksums (sha256 64-hex, path traversal guard), SBOM carried metadata, signature carried metadata (明确诚实：integrity NOT authenticity), dependency ranges + reportDependencyStatus。 | **大部分已有** | 依赖检查（WP 已存）与跨平台路径已有；增量：signature metadata 的类型化 schema 校验（当前是 note 级） |
| WP7 hostile 测试 | 大量存在 (test_plugin_host_process.cpp 25 TEST_CASEs)。 | **大部分已有** | **重复 N 轮 load/unload/crash/restart 后计数回基线的循环 Oracle 缺失**；restart exhaustion + concurrent close 组合测试缺失 |

## 与远端残留分支的关系

- agent/glm53-plugin-sdk-trust: 其 fix 意图（#1039/#1040/#1041/#1036 + lifecycle holes）已由 master d2a723ee2 (#1103) 以不同实现 supersede。**不 cherry-pick**。
- 其余 agent/*, fix/*: 均已 supersede 或merged via r2-* PR 链。

## 与并行 Track 的关系

- Workbench Track 投影 plugin commands：双方经 CommandRegistry/UI schema seam，本 Track 不改 app 窗口/ribbon。
