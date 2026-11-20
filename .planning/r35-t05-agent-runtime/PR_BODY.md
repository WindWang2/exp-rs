# Agent 运行链：proxy 请求身份与输入边界（R35-T05）

## 原问题与影响

Agent/Python 通道上四个已实证的输入身份与边界缺口（均在 base `120677e6` 上逐条核实并复现）：

1. **proxy 迟到响应误投（WP11）**：`worker_daemon.py` 的 `canvas.get_state`/`catalog.get_active_layer` 使用固定请求 id（8001/8003）。前一次调用超时后，迟到的旧响应仍带着同 id 到达，被**下一次调用当作自己的答案**消费——研究阶段以真实 AF_UNIX socketpair 复现：B 接受了 `generation=old-A` 的过期状态。canvas 视图/激活图层查询可能拿到跨调用的陈旧数据。
2. **非法 UTF-8 被洗白（WP13）**：完整行以 `errors="replace"` 解码，非法字节（如 `0xff`）被改写为 U+FFFD 后按合法 JSON 接受（真实字节传输+生产 decoder 复现）——损坏的路径/名称被当作真实答案。同时 `tests/test_worker_daemon_proxy.py` 的 FakeSocket 缺 `settimeout`，#649 的三条回归在 HEAD 上 3 errors（exit 1，实测复现），形同虚设。
3. **shm 挂载不校验 header（WP14）**：`_mount_shm_array` 按请求参数 mount，不读段自身 32-byte header——header UInt8 + 请求 Float32 时被静默按 float32 读值（真实 40-byte POSIX mapping 复现）；拒绝路径泄漏 mapping 句柄。C++ `SharedMemorySegment::create()` 的 `width*height*bands*elemSize` 无溢出检查：`2^30×2^30×4×4=2^64` 回绕为 0，"巨大维度+32 字节微分配"被接受（attach() 侧自 R28-T03 起已有完整守卫，create() 是镜像缺口）。
4. **大数组先物化后被拒（WP15）**：`_json_safe` 对任意 ndarray 先 `tolist()`，1e8 元素数组先建多 GB Python list，再由 1 MiB wire 检查拒绝——纯内存尖峰。
5. **SSE 分片改变语义（WP02）**：`LlmStreamingClient::onReadyRead` 在行切分**前**对整个缓冲判 8 MiB 单行上限——一次合并交付的多条各自合法小行总量超限即被整体误拒为 `oversized_line`（网络分片方式改变了解析结果）。

## 已合并基线

`120677e6`（含 #2143 Mission/journal/sidecar、#2147 执行/验证等）；本 PR 从 fresh master 独立开工，无 stacked 依赖。

## 本次增量

- **WP11**：`_get_state`/`activeLayer` 改为**每次调用单调递增唯一 id**（10000 起，高于 fire-and-forget ACK 域——R1 发现初版 8000 起会与 8002/8004/8005 的 host ACK 碰撞，已修+回归）。迟到响应不再匹配任何在途请求，由主循环按既有 DATAPY-11 drain 契约丢弃。
- **WP13**：三处解码点（两个 proxy 读循环+主循环）完整行 strict UTF-8——解码失败即丢弃该行（stderr 记录），不再 U+FFFD 洗白；#649 合法多字节跨 recv 契约由恢复后的三条回归+新套件保持。
- **WP14**：Python 侧 `_mount_shm_array` 以**段自身 header 为权威**（dtype 必须匹配、请求维度非空时必须一致、容量显式校验、失败 `shm.close()` 无句柄泄漏、creator/attacher unlink 职责不变）；`shm.read` 对 ValueError 返回既有 result-error 形状。C++ 侧 `create()` 镜像 attach() 的逐步 checked multiply + header 加法守卫。
- **WP15**：`_json_safe` 对 ndarray 先按 **dtype 感知**预算（整数=np.iinfo 位数+2、浮点=26+2）拒绝超额数组（R1 发现初版统一 32B/元素误拒 200×200 uint8 掩膜，已修+回归），再 tolist；wire 1 MiB 检查保持权威。
- **WP02**：`onReadyRead` 重构为"先排干完整行、只对**无换行尾部**判 8 MiB"——合法多行批量不再被误拒，敌意无换行流保护不变；新增公共 seam `ingestTransportBytes`（与 `parseSseLine` 同一测试 seam 先例）承载确定性分片不变性回归。
- 夹具修复：FakeSocket 补 `settimeout`（恢复 3 条 #649 回归）；`.planning` 白名单与 `tests/CMakeLists.txt` 追加块（append-only）。

## 验证（本 worktree 实测）

| 命令 | 结果 |
|---|---|
| `python3 tests/test_r35_t05_worker_daemon_identity.py` | **15/15 PASS**，exit 0（迟到误投×3、ACK 碰撞、UTF-8×2、真实 POSIX shm×6、预算×3） |
| `python3 tests/test_worker_daemon_proxy.py` | **3/3 PASS**，exit 0（HEAD 原版=3 errors exit 1，实测复现） |
| **阴性对照**（同一套件 vs HEAD 未修复 daemon） | **10/13 FAIL**——四个缺陷 oracle 全部失败，3 个通过项恰为既有正确行为（测试真实咬合） |
| `ctest -C Debug -j1 -R "r35_t05\|LlmStreamingClient\|LlmConfigManager\|corpus SSE\|chunking\|oversized\|usage…"` | **30/30 passed**，exit 0（含新 C++ ×2、既有敌意 transport 回归、新分片不变性回归） |
| 构建 | dev-default（Debug）双 resource_guard `-j4`，`test_r35_t05_agent_runtime_bounds test_llm_streaming_client` exit 0 |

## 独立审查

- **R1**（独立 subagent，读全量 diff+调用方+亲自跑测试）：REQUEST_CHANGES —— P1 id 碰撞（reviewer 实证复现）+ P2 预算误拒，均已在 `d0d1353435` 修复并各加回归；P3×3 记账（error 响应吞 body 为既有行为，列 HANDOFF）。
- **R2**：复查修复 commit 与最终 diff（结论见 `.planning/r35-t05-agent-runtime/REVIEW_R2.md`）。

## 边界与兼容

- IPC wire 格式不变（32-byte header 布局、JSON-RPC 形状、fire-and-forget id 8002/8004/8005/9999 原样）；在树内全部 shm 发送方 header 一致，无误拒（R1 逐调用方核实）。
- 无新线程、无新权威/调度器；Qt 生命周期与 reentrancy 守卫原样保留（R1 逐项验证）。
- 未含：Native 累计预算（WP03）、dock 批量渲染（WP04）等后续工作包；E2E 完整 daemon/GUI 腿（本机 AF_UNIX listener EPERM，ENVIRONMENT_BLOCKED）。

## 元数据

- base `120677e6`（= origin/master，提交前核实未移动）；无 rebase 需要。
- 与在途 PR #2149/#2150/#2151/#2152 零整文件碰撞；共享点仅 `.gitignore`/`tests/CMakeLists.txt` 各自追加。
- 复现脚本路径：`tests/test_r35_t05_worker_daemon_identity.py`（阴性对照方法见 LEDGER R2）。
