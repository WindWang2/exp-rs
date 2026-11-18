# ACCEPTANCE — r35-t05-agent-runtime

Base `120677e6e165d3e88bb9d2ec2a3fd98042ec719d`。以下每项命令在本 worktree 真实执行，数字为实测（最终 PR 前如有重跑以最后一次为准并更新本表）。

## 逐项 oracle 实测

| 项 | 命令 | 结果 |
|---|---|---|
| 新 identity 套件（迟到误投/UTF-8/shm/json 预算） | `python3 tests/test_r35_t05_worker_daemon_identity.py` | **13/13 PASS, exit 0** |
| 修复后的旧 proxy 套件（#649 回归恢复有效） | `python3 tests/test_worker_daemon_proxy.py` | **3/3 PASS, exit 0**（HEAD 原版=3 errors, exit 1 实测复现） |
| 阴性对照（同一套件 vs HEAD 未修复 daemon） | 见 LEDGER R2 | **10/13 FAIL**（四个缺陷 oracle 全部失败；3 通过项为既有正确行为）——证明测试真实咬合 |
| C++ shm create 溢出拒绝 + 合法路径 | `ctest -R r35_t05_bounds` （见下）|
| SSE 分片不变性（burst/chunk/敌意尾流） | `ctest -R test_llm_streaming_client`（新 TEST_CASE + 全部既有 case） （见下）|
| 构建目标 | 双锁 `--parallel 4` `test_r35_t05_agent_runtime_bounds test_llm_streaming_client` | 待填 |

## E2E 矩阵（本 PR 范围）

| 链 | 状态 | 说明 |
|---|---|---|
| E2E-01 Native 长流（部分） | PARTIAL | SSE 分片无关性 + 敌意尾流拒绝在真实 transport/seam 层验证；真实 HTTP loopback 既有回归（8 MiB 敌意）保持。工具执行链不在本 PR。 |
| E2E-04 daemon 请求身份/shm（部分） | PARTIAL-FAKE-SOCKET | 真实 POSIX shm 段上验证 header 权威/容量/无 fd 泄漏；proxy 身份用确定性 ScriptedSocket（真实 AF_UNIX listener 本环境 EPERM，ENVIRONMENT_BLOCKED）。 |
| E2E-02/03/05 | NOT_EXECUTED | 本 PR 未触碰；后续工作包。 |

## 明确未执行

完整 CTest 全量、可见 Qt 桌面、真实 ORT/GPU、Windows、真实云模型、真实 daemon 全进程链（listener EPERM）。

## 构建与 CTest 最终数字（多轮一致）

- configure：dev-default（Debug）+ `-DOpenCV_DIR=.../ui-build-deps/opencv/usr/lib/cmake/opencv5`，exit 0。
- 构建：双 resource_guard `-j4`，目标 `test_r35_t05_agent_runtime_bounds`、`test_llm_streaming_client`，exit 0（~2300 TU，含 qgis_core/gui 依赖链）。
- `ctest -C Debug -j1 -R "r35_t05|LlmStreamingClient|LlmConfigManager|corpus SSE|chunking|oversized|usage|nested|real HTTP callback|re-entering"` → **100% tests passed out of 30**（R1 修复前 30/30、polish 后最终 HEAD 复跑 30/30）。
- `ctest -R "r35_t05"` → 3/3（bounds×2 + identity）。
- 关键 Python 回归第二遍+新临时目录（/tmp/r35t05_recheck）复验 exit 0。
- R1 独立 reviewer 亲跑：15/15、3/3、ctest 4/4（R2 轮）。
