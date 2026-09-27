# PR BODY (draft) — hardening/r4-mcp-surface

> 提交时以此为基础填充实测数字。

## Title

fix(mcp,pi): lock the three-surface tool contract with a shared snapshot; harden MCP protocol edges (Track 9 R4)

## 基线与范围

- 基线：origin/master `15e5c66b5`（实测，worktree `../exp-rs-mcp-surface-r4`）
- 与 #1334 的边界：本轨道**未做任何安全改动**（无 default-deny、无 trust-gate、无 workspace containment 语义变化）。文件级隔离：#1334 修改的 `tests/test_surface_protocol.cpp` 本轨道零触碰（新用例在 `test_surface_protocol_r4.cpp`）；`surface_registry.*`、`meta_protocol_tools.cpp` 零改动；`exp-rs-spatial.ts` 仅生产逻辑等价的 helper 搬迁（piToolName/truncateTail → mcp_bridge.ts，#1334 对该文件的改动仅头部注释，自动合并）。
- 零新方向：无新工具、无新前缀、无新类目；diff 中 mcp_server.cpp 唯一行为变更是缺 method 帧 -32601→-32600（JSON-RPC 2.0 合规修复，见提交 N）。

## 三处表面 diff 清零证据

- 比对器：`tests/surface_diff_snapshot.json`（schemaVersion 1，N 个工具）+ 两侧消费：
  - C++：`test_surface_snapshot` 6 用例（registry ↔ projection ↔ tools/list wire ↔ snapshot 四方逐字段）
  - pi：`pi/test/surface_snapshot.test.mjs` 6 用例（类目新鲜度 / 映射单射 / 名称冲突 / 消费合同 / schema 往返 / 截断合同）
- 自证：故意改快照一端 → 两侧红；还原 → 绿（记录见 EVIDENCE.md §红测自证）
- 再生成：`ninja -C build-r4 surface_snapshot_regen`（CMake 注释 + 每条红消息内联）

## 协议健壮性 16 用例

C++（test_surface_protocol_r4.cpp，10 用例）：解析故障 -32700 四联（垃圾/标量/批数组/超长哨兵）、缺 method -32600（含修复）、-32002 门三态、显式 null id 合同、通知不应答、arguments 非对象 -32602、未知工具 -32602 具名、cancelled 一次性+映射+不泄漏、spatial 缺参/错型具名 INVALID_PARAMETER、tools/list 前缀过滤合同、schema 往返。
pi（mcp_bridge.test.mjs +6）：abort 竞态恰一次 cancelled、late-reply 不泄漏、pre-aborted、非 JSON 行容忍、结构化错误透传、60k 传输保真。


## 顺带修复（master 基线红）

`node --test pi/test/` 在 master 上 14过/5红——5 个结构测试落后于桥单实现重构（行为测试全绿）。本 PR 将其 pin 到当前形状（提交 38e47e71d）。附：mcp_server.cpp 缺 method 帧从 -32601 修正为规范 -32600。

## 本地验证（未等待线上 CI）

- 构建目录：`build-r4`（全新，ENABLE_TESTS=ON，Debug，ninja -j2）
- ctest: `ctest --test-dir build-r4 -R "surface|mcp|protocol|parity" -E "_NOT_BUILT|launcher" -j1` → **25/25 × 2 轮**（launcher_parity_conformance 为 master 同样失败的环境性问题，python3.14 subprocess 起脚本；NOT_BUILT 项为未纳入本轨道构建的目标）
- node: `node --test pi/test/` → **32/32 × 2 轮**
- 快照红测自证记录
- 资源红线：全程 -j2（RAM 64GB，未触发 70% 降级）

## Backlog / 未解决项（评审采纳、留 follow-up）

- CountingServer #644 守卫镜像的机械失同步报警（经 surface_mcp_host 真实 stdio 路径的守卫冒烟用例）。
- intentional-unbridged families 清单的自动化一致性校验。
- launcher_parity_conformance 环境性失败（python3.14 subprocess；master 同样失败，测试基建领地）。
- 独立评审全记录见 .planning/mcp-surface-r4/REVIEW_LOG.md（P0=0/P1=2/P2=11，P1 全修、P2 修 9 入 3）。

## 提交清单（12）

9135dd1a4 规划 / 38e47e71d 陈旧 pi 测试修复 / 5ef9e9f39 helper 单源化 / f2af2cd4e WP-D pi 用例 / 05f538ff3 链接修复+注册 / fe1c66631 WP-A 快照锁 / 0b5081265 -32600+WP-C / 783909f6c WP-B pi 腿 / 98cdedd58 host 链接修复 / 6a9100752 评审修复 R1(C++) / cf4a01643 评审修复 R1(pi) / 最后 planning 工件
