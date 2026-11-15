# Pi 产品 Agent 实施记录

基线：`8d36bfd243476a445a89551d4fa18b5f7a0b5e9f`。实施方案：`docs/superpowers/plans/2026-11-15-vendored-pi-product-agent.md`。

用户已授权按方案实施。按 P0 → P5 顺序推进，阶段通过后继续，不把仅能回答文本或独立 MCP 产物当作产品闭环完成。

| 阶段 | 当前状态 | 验证要求 |
| --- | --- | --- |
| P0 固定 SDK、认证隔离与兼容验证 | 实施中 | 固定依赖、认证透传 red/green、受控模型/工具/取消/会话、本地服务；不使用真实密钥 |
| P1 随包 Node/Pi、私有协议和 CMake | 待 P0 通过 | 严格 receipt、离线准备、安装/搬迁、损坏负例、进程生命周期 |
| P2 当前 Qt 宿主工具与对话接口 | 待 P1 通过 | 同一 GUI Task Center/DataManager、审批与取消、无重复执行 |
| P3 双语 Python NDVI 产品闭环 | 待 P2 通过 | 两语言真实 Python 像元、任务、资产、地图与退出保留 |
| P4 真实模型、分阶段默认 | 待 P3 通过 | 真实模型链路与确定性 fixture 分开报告、资源与成本记录 |
| P5 合并后 master Windows 发布 | 待 P4 通过 | 新包完整性、无系统 Node/npm/Python、可见双语交付 |

## 已固定的准备条件

- Pi SDK 候选 `1.5.2`，上游提交 `b4afdddf44e059c67ccf5097e5317d913521b470`。
- Node Windows x64 `24.22.3` 官方 ZIP 已下载并核对 SHA-256：`e612af99f626ab415ea225af6bce5c971feb448cb5cae250bb1c3fa50c5f7d9f`。
- 编写测试的公开接口已随方案获准：`ModelRuntime.create/registerProvider`、受控会话 prompt/tool/abort、运行时 prepare/verify-stage；不测试私有字段。
- 只由主任务运行 C++ 构建与整套测试；本机构建并行上限 2、测试串行，使用 `resource_guard.py`。
- SDK 准备禁止 npm 生命周期脚本；真实用户 provider、Pi 配置、认证文件和密钥不用于 P0。

## 阶段记录

### P0

正在准备完整生产依赖锁、`AuthContext` 透传小补丁、受控 SDK 模块与本地 OpenAI 兼容验证。尚未声明 SDK、离线闭包或产品接入通过。

正式结果、执行命令、源码与发行物身份、剩余问题和 PR 在阶段完成后追加。本机临时工具及原始日志放忽略的 `build-logs/`，不作为发布脚本的依赖来源。
