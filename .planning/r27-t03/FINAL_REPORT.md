# R27-T3 FINAL REPORT — Pi/MCP 空间 Agent、审批权限、工具安全与 SDK/插件闭环

- BASE_SHA: `10652fd003c66ae78598d2b878eed9beb50029fa`（origin/master, 侦察 2026-11-02T13:24Z；PR 前复核无漂移）
- HEAD: 见下方 PR 块；分支 `agent/r27-t03-agent-security-sdk-plugins-20261102T132444Z`
- Worktree: `../exp-rs-r27-t03-20261102T132444Z`（本轨唯一）

## 处理 Issues（13 全 LIVE → 全部修复或明确记录）
| Issue | 状态 | 摘要 |
|---|---|---|
| #1733 P0 AS-007 | **修复** | 审批 token v2：nonce + HMAC-SHA256/16（进程内密钥，与 python3 独立实现 KAT 交叉验证）；TTL≤30min 拒绝超限；consumed 环仅过期逐出、满员 fail-closed；session surface/OpsDriver/MCP 三层拒绝 `APPROVAL_HUMAN_CHANNEL_ONLY`；唯一 mint = 本地 UI（dock Approve → worker 在 coordinator 线程 mint+arm） |
| #1741 P1 AS-013 | **修复** | 未知 driver payload 逐段扫描（绝对/引号/vsi/scheme/Windows 盘/kv/`..`/`~`）；GPSBABEL 设备类显式拒；14 逃逸形矩阵 + 良性回归 |
| #1742 P1 AS-006 | **修复** | dock 持久串行 executor（单线程 + 单深替换队列）：任务永不重叠（100× 重启 maxInFlight==1、违规==0），startWorker 不阻塞 GUI，析构取消+清队+一次 join |
| #1760 P2 AS-008 | **修复** | arm 即时 shape 校验 typed 拒绝（首跑抓到真 bug：TTL 超限因指针比较误报 MALFORMED）；mint/arm 全部裁决经 approvalStatusChanged 入 panel |
| #1761 P2 AS-009 | **修复** | LD_/DYLD_ 前缀族 + BASH_ENV/ENV/NODE_OPTIONS/PYTHON*/PERL*/RUBY*/GLIBC_TUNABLES/GCONV_PATH/JAVA_*/BASH_FUNC_ 前缀；良性 GDAL_*/OMP_* 对照 |
| #1762 P2 AS-010 | **修复** | 键名分隔符归一（api-key/x-api-key）；值前缀形状脱敏（Bearer/sk-/ghp_/xox*- 连字符/PEM）；科学证据对照不动 |
| #1763 P2 AS-014 | **修复** | copilot 单调用门（dispatcher-parsed 参数，R1-F1 修正真实 wire 形状 + 字符串 blob）；写槽 → 人工审批卡（交互动作豁免，R2 守卫） |
| #1796 P2 SPP-1 | **修复** | 单一 try/finally 统一回收 shm；tile 坏 → -32000 且好段 unlink（实测断言 /dev/shm 消失） |
| #1797 P2 SPP-2 | **修复** | 先提交 loaded_plugins 再 initGui，失败回滚 executors/callbacks → -32001；孤儿 py: → -32602（实测） |
| #1798 P2 SPP-6 | **修复**（分流修正：base 已挡三输入，真缺口=退出码+零 catch） | invalidInput 标志 + runFromJson Json::LogicError 边界；CLI 实测四形状全 exit 6（原为 1/134 风险） |
| #1799 P2 SPP-7 | **修复** | OPERATORS 记录声明模块；publish_to_iface 仅发布调用插件（含包子模块）；A/B 隔离实测 |
| #1802 P3 | **修复 AS-011/015**；AS-016 symlink TOCTOU 记录为残余（需改各 surface 打开点，超本轨最小面） | bridge env allow-list；非回环 http+Bearer 拒发（全 127/8） |
| #1806 P3 | **修复 SPP-3/8/11**；SPP-5 复核为已有防护；SPP-9 部分（dtype 拒绝）；SPP-10/12/13 记录不做（跨进程契约/独立设计） | dtype=99 typed 拒；@staticmethod；client 超时独立 kind(8) + timeout=0 ValueError |

## 前置修复（未列 Issue 但阻塞全轨）
master `tests/CMakeLists.txt` 合并撕裂（17426 起 parse error + 丢 31 行注册）：base608 + 八轨净 diff 重建，多重集/目标集/配置三重证明（RECONSTRUCTION.md）。

## 评审
- R1（独立子代理，REJECT→修复）：F1 门形状 no-op（P0）、F2 barrier、F3 环、F4 oracle、F5-F7/F10；全部修复重测
- R2（主会话按独立清单，子代理通道配额中断已如实记录）：验证 R1 全项 + 新发现交互动作误入审批卡（P2）已修；**approve**

## 本地验证（全部实测，双跑）
- `ctest`（69 例选择器：7 个新 C++ 二进制 + CLI oracle + python worker + 4 个更新既有套件）：**100% ×2**；R2 守卫后复跑 **69/69**
- python worker 直跑：4/4 ×2；r22 邻接（containment 78 断言/env_keys 46 断言）绿
- HMAC KAT vs python3 hmac：一致
- TSAN：本机无 → NOT_EXECUTED（以确定性 100× 串行断言代替，不伪称）
- 构建：GCC16 ICE 由仓库自带重试器消化；BFD 2.47 DWARF reloc bug 以本地 `-fuse-ld=lld` 绕过（仅 build cache，仓库零改动）

## 平台未测
Windows/macOS 路径分支仅静态覆盖（GTIFF_DIR C:\ 形在单测矩阵）；远端 CI 未等待。

## 写入面核验
改动 52 文件全部位于白名单（src/agent、src/agent_ops、src/app/agent_ops、src/cli、src/python 既有 worker/SDK、src/sdk、pi/、tests 本轨 + tests/CMakeLists 重建&追加、.planning/r27-t03）；根 CMakeLists/.gitignore/docs-adr 零改动。
PR-URL https://github.com/WindWang2/exp-rs/pull/1808
