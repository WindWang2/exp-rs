# REVIEW_LOG — Track 9 (mcp-surface-r4)

评审方式：1 个独立对抗性 subagent（只读 + 带证据挑错），双轴（Standards / Spec），对照 `origin/master 15e5c66b5` 全部 diff（17 文件）。评审时点：修复轮之前（故部分 P2 已在评审时段后被修复提交覆盖）。

## 评审结论

**P0 = 0，P1 = 2，P2 = 11。生产代码（mcp_server.cpp 12 行 + pi 纯函数搬迁）逐行核查正确、最小、无 #1334 越界。**

## 逐条处置

| # | 级别 | 发现 | 处置 | 落点 |
|---|---|---|---|---|
| 1 | P1 | cancelled 用例空路径（映射表从未填充，"maps the rpc id" 名不副实） | **已修**：用例经真实 execute_operator 填充 rpc-id→task 映射后再验一次性/不投毒；改名如实（TaskCenter cancelTask 效果由 e2e 覆盖的说明入注释与 EVIDENCE） | 6a9100752 |
| 2 | P1 | 提交数 9 < 10（自定门） | **已修**：评审修复自然补足 → 最终 12 提交 | 6a9100752, cf4a01643, planning 提交 |
| 3 | P2 | -32600 检查在 -32002 门后，错误码随服务器状态分裂 | **已修**：帧分类前移至门之前（Invalid Request 约束帧，门约束请求）；新增未初始化服务器上的 -32600 用例；D-3 更新 | 6a9100752 |
| 4 | P2 | 快照 vs projection 腿构造性同义反复（非 spatial 切片无独立真值） | **已修**：新增 known-answer 三工具独立锚（meta/data-platform/spatial，手写期望，不经生成器） | 6a9100752 |
| 5 | P2 | pi 消费合同靠 regex 影子 + toolCategory 孪生（违背 D-8 单源原则） | **已修**：toolCategory 搬迁 mcp_bridge.ts，测试 import 真函数 | cf4a01643 |
| 6 | P2 | 硬编码工具收集正则 `[a-z_]` 窄于合法名集 | **已修**：`[a-zA-Z0-9_-]+` | cf4a01643 |
| 7 | P2 | 固定 sleep + `<=1` 上界断言（竞态脆弱 + 漏检双发） | **已修**：轮询至 deadline；pre-abort 收紧为恰一次；顺带发现并修复该测试 bridge 未接 notify-log 的构造遗漏 | cf4a01643 |
| 8 | P2 | CountingServer 手工复刻 #644 守卫无失同步报警 | **已修（注释级）**：双向 KEEP-IN-SYNC 交叉引用（机械报警留 backlog） | 6a9100752 |
| 9 | P2 | catalog 探针清理不在异常安全路径 | **已修**：RAII 注销守卫（Unregister 析构）；注释如实描述全栈验证语义 | 6a9100752 |
| 10 | P2 | 修改了既有 master 目标块，违背 D-1 "既有行不动" | **已修（文档）**：D-1 补记偏差与理由（链接修复为 master 同源缺口，#1334 冲突面评估不变） | planning 提交 |
| 11 | P2 | WP-B 类目只交付单向（新 C++ family pi 零感知） | **已修**：反向门——快照 family 必须 ∈ 桥接 ∪ 显式不桥接清单（新 family 强制显式决策） | cf4a01643 |
| 12 | P2 | 损坏快照下 jsoncpp 抛未捕获 LogicError，regen 提示失效 | **已修**：coercion 前置检查 + 统一 hint 路径 | 6a9100752 |
| 13 | P2 | `assert.equal(settled, true)` 是同义反复 | **已修**：改为直接监视迟到 resolve（no-leak 本体） | cf4a01643 |

## 评审员明示核查无发现项（复述备查）

- -32600 修复最小性、通知静默不变、-32002 门代码未动；红绿流程成立。
- #1334 边界：零触碰 sandbox/trust-gate/containment；零新工具/前缀/类目；17 文件全在白名单。
- 38e47e71d（5 个陈旧测试修复）= 去陈旧化而非掩盖缺陷（对照 master 源逐条验证）。
- C++ 11 用例 / pi 6 用例达标，错误码断言独立于实现（JSON-RPC 2.0 字面量）。

## Backlog（评审采纳但留 follow-up 的项）

- CountingServer 守卫失同步的机械报警（经 surface_mcp_host 真实 stdio 路径的守卫形状冒烟用例）。
- intentional-unbridged 清单与 CMake/DREADME 的自动化一致性（当前人工维护）。
- launcher_parity_conformance 的环境性失败（python3.14 subprocess 起脚本，master 同样失败）属测试基建，非本轨道白名单焦点。
