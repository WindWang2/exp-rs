# EVIDENCE — Track 9 (mcp-surface-r4)

环境：worktree `/home/kevin/project/exp-rs-mcp-surface-r4`，基 `origin/master 15e5c66b5`；构建 `build-r4`（全新，ENABLE_TESTS=ON，Debug，ninja -j2，CMAKE_PREFIX_PATH=/home/kevin/pwb-sdks/root/usr）。测试环境：`QT_QPA_PLATFORM=offscreen LD_LIBRARY_PATH=/home/kevin/pwb-sdks/root/usr/lib:/usr/lib`。

## 1. 提交链（origin/master..HEAD，9 个原子提交）

| 提交 | 内容 |
|---|---|
| 9135dd1a4 | Phase 0 规划工件（BASELINE/PLAN/DECISIONS） |
| 38e47e71d | 修复 master 基线红的 5 个陈旧 pi 结构测试（D-9） |
| 5ef9e9f39 | piToolName/truncateTail 单源化搬迁（D-8） |
| f2af2cd4e | WP-D pi 6 用例（取消竞态/帧容错/错误透传/传输保真） |
| 05f538ff3 | 链接修复（Sicnu::agent_ops）+ Track 9 目标注册 |
| fe1c66631 | WP-A 快照锁：生成器 + 比对器 + 快照 + ADR 0122 注记 |
| 0b5081265 | WP-C：-32600 缺陷修复 + 协议健壮性 11 用例 |
| 783909f6c | WP-B pi 腿：共享快照消费 6 用例 + fake server 解析修复 |
| 98cdedd58 | surface_mcp_host 同类链接修复 |

## 2. 三处表面 diff 清零证据

- 快照 `tests/surface_diff_snapshot.json`：**371 工具 / 34 families / schemaVersion 1**（hermetic in-process projection）。
- `test_surface_snapshot` 6 用例 16,000 断言全绿：registry ↔ projection ↔ tools/list wire ↔ snapshot 四方逐字段（name/family/description/inputSchema，canonical JSON，键序无关，顺序钉死）。
- pi 腿 `surface_snapshot.test.mjs` 6 用例全绿：类目新鲜度、映射单射、名称冲突、消费合同、schema 往返、截断合同（真函数 import）。
- **实测锚定终值**：`spatial:` 注册工具 **17 个**（写作值 8 为陈旧口径，含 spatial:understand 等）；白名单前缀 35；pi 默认类目 11 ⊆ 实测 families。

## 3. 比对器红测自证（WP-E）

| 变异 | 预期 | 实测 |
|---|---|---|
| 快照内 `spatial:layer_summary` → `..._mutated` | C++ 比对器红 | test_surface_snapshot：3 用例红（projection/wire/registry pin）✅ |
| 壳默认类目 `harness` → `harvest` | pi 陈旧类目红 | surface_snapshot.test.mjs：stale-category 红 ✅ |
| `ninja surface_snapshot_regen` 再生成 | 与提交版一致、恢复绿 | **字节级一致**（git diff 空）；6/6 绿 ✅（生成器确定性自证） |

## 4. TDD 红证据（-32600 缺陷修复）

- `git stash src/agent/mcp_server.cpp`（还原 master 形状）→ `a request frame without a method member` 用例：**-32601 == -32600 FAIL**。
- 恢复修复 → 同用例绿。缺陷与修复均为最小面（handleRequest 插入 12 行，位于 -32002 门之后）。

## 5. 双跑门禁（Oracle-2）

```
=== ctest round 1 === 100% tests passed, 0 failed out of 24  (14.59s)
=== ctest round 2 === 100% tests passed, 0 failed out of 24  (12.38s)
  集合：ctest -R "surface|mcp|protocol|parity" -E "_NOT_BUILT|launcher" -j1
  （launcher_parity_conformance 为环境性失败：python3.14 subprocess 无法
    起 GRADE_ALL.sh 脚本，master 同样失败，与本轨道改动无关）
=== node  round 1 === tests 31 pass 31 fail 0
=== node  round 2 === tests 31 pass 31 fail 0
```

### 协议健壮性用例逐条清点（≥15 达成：17+6）

C++ `test_surface_protocol_r4`（11 用例，2597 断言）：
1. parse faults -32700 四联（垃圾/顶层标量/批数组 -32600/4MiB 哨兵）
2. 缺 method 请求 -32600（缺陷修复本体）
3. -32002 预初始化门（tools/list 拒 / ping 豁免 / 通知豁免）
4. 显式 null id 应答 id:null（结果 + 错误双路径，#644）
5. 通知永不应答（未知 method / 未知工具，#644/#620）
6. tools/call arguments 非对象 -32602
7. 未知工具 -32602 具名
8. notifications/cancelled 一次性 + rpc-id 记账安全（#634/#644）
9. spatial 缺必填参 → isError + INVALID_PARAMETER 分类码 + 字段名
10. spatial 类型错位 → 同合同（#620 declared-type check）
11. tools/list 前缀过滤合同 + 嵌入 schema 对象根

pi（mcp_bridge.test.mjs，6 用例）：
12. abort 竞态：恰一次 notifications/cancelled + 状态复位
13. abort 后迟到应答被丢弃（不泄漏不双发）
14. pre-aborted signal 即拒
15. 非 JSON 行容错（帧同步存活、子进程不死）
16. 结构化工具错误透传（isError/errorCode/errorCategory，#645）
17. 60k 传输保真（bridge 不截断，MAX_RESULT_CHARS 是壳的预算）

另：pi surface_snapshot 6 用例（消费合同/往返/截断标记格式）与 C++ snapshot schema 往返用例提供第 18-24 层加固。

## 6. 与 #1334 的边界审计

- 本轨道对 `src/agent/mcp_server.cpp` 的唯一改动 = 缺 method 帧 -32600（`git diff 15e5c66b5 -- src/agent/mcp_server.cpp` 可见，12 行，无 sandbox/trust-gate/containment 语义）。
- `test_surface_protocol.cpp` / `test_surface_e2e.cpp`（#1334 领地文件）零触碰；新用例在独立文件。
- `surface_registry.*`、`meta_protocol_tools.cpp`、`workspace_containment.h`：零改动。
- `pi/exp-rs-spatial.ts`：仅 helper 搬迁（行为等价，pi 31/31 背书）；#1334 对该文件的改动在头部文档注释区，不相交。

## 7. 评审后最终状态（Phase 5 修复轮）

- 评审结论 P0=0 / P1=2 / P2=11；P1 全修，P2 修 9 项、3 项入 backlog（详见 REVIEW_LOG.md）。
- 快照 371 工具/34 families 不变；`test_surface_snapshot` 7 用例 17,872 断言全绿（+known-answer 独立锚）；`test_surface_protocol_r4` 11 用例 2,606 断言全绿（-32600 前移至 -32002 门之前 + cancelled 用例真实映射填充）；pi 32/32（toolCategory 单源 + 反向类目门 + 竞态轮询）。
- **修复后最终双跑**：ctest `-R "surface|mcp|protocol|parity" -E "_NOT_BUILT|launcher" -j1` = **25/25 × 2 轮**；`node --test pi/test/` = **32/32 × 2 轮**。
- 提交总数：**12**（origin/master..HEAD）。

## 8. 资源红线

全程 `ninja -j2`（RAM 64 GB，峰值远低于 70%，未触发 -j1 降级）；ctest `-j1`；零 subagent 递归（评审用 1 槽位）；未等待线上 CI；未触碰白名单外目录。
