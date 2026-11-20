# BASELINE — r35-t05-agent-runtime

- 创建：2026-11-19（UTC），执行器 ZCode，goal-loop 方法论。
- 研究快照出处：2026-11-18 22:35 CST，master `120677e6e165d3e88bb9d2ec2a3fd98042ec719d`，0 open PR。
- 本轨 fresh worktree base：`120677e6e165d3e88bb9d2ec2a3fd98042ec719d`（= origin/master，fetch 后核实）。
- 执行时 open PR（与研究快照不同，新增 3 个）：
  - #2149 head `dc9dec27a7`（T06 desktop）——写集：`.gitignore`、`.planning/r35-t06-*`、`resources/translations/sicnu_zh_CN.ts`。
  - #2150 head `36dcea9106`（T01 science）——写集：`src/processing/algorithms/*`、`tests/CMakeLists.txt`（追加块）、`tests/test_r35_t01_*`、`.gitignore`、`.planning/r35-t01-*`。
  - #2151 head `95e54e5485`（T03 data-io）——写集：`src/dataset/*`、`src/geospatial/*`、`tests/CMakeLists.txt`（追加块）、`tests/test_r35_t03_*`、`.gitignore`、`.planning/r35-t03-*`。
- 与本轨主写集无整文件碰撞；共享点仅 `.gitignore` 与 `tests/CMakeLists.txt`（各自追加独立块，不重排他人块）。
- 本 HEAD 上逐条核实的源码事实（`SOURCE_CONFIRMED`，全部在 `120677e6` 上直接读取）：
  1. `src/python/scripts/worker_daemon.py:105` `req_id = 8001`（`_get_state` 固定 id），`:243` `req_id = 8003`（`activeLayer`）。两个 request/response 相关调用复用同一 id → 前一次超时后的迟到响应会被下一次同 id 调用误收（研究阶段 real AF_UNIX socketpair 探针已复现；本 HEAD blob 与 e679 相同）。
  2. `worker_daemon.py:164,289,876` 对**完整行**使用 `errors="replace"` 解码 → 非法 UTF-8（如 `0xff`）被改写为 U+FFFD 后按合法 JSON 接受（real socketpair 探针已复现）。合法多字节跨 recv 已由 complete-line framing 保护（#624/#649 回归基线）。
  3. `worker_daemon.py::_mount_shm_array`（:65-92）不读 32-byte header：请求 dtype/维度与 header 不一致时按请求值 mount（e679 探针：header UInt8 + 请求 Float32 被 float32 读值）；无显式容量校验；校验失败路径无 `shm.close()`（句柄泄漏）。C++ `attach()` 已有完整 header/checked-multiply/容量校验（`shared_memory_segment.cpp:152-207`，R28-T03 WP09）。
  4. `src/python/isolated/shared_memory_segment.cpp:59` `create()` 的 `static_cast<size_t>(width)*height*bands*elemSize` 无溢出检查：`2^30×2^30×4×4=2^64` 回绕为 0 → `totalSize=32` 通过 `totalSizeQ>0` 检查，create(32) 成功，"巨大维度+微分配"被接受（attach() 的镜像缺口）。
  5. `src/agent/llm_streaming_client.cpp:277` `onReadyRead` 在**行切分前**对整个 `m_buffer`（readAll 累计）判 `>kMaxSseLineChars`(8MiB) → 一次交付的**多条各自合法小行**总量超 8MiB 会被整体误拒为 `oversized_line`。`parseSseLine` 已有单行 bound（:317）；transport 回归（tests/test_llm_streaming_client.cpp:742）只覆盖"无换行敌意尾流"。
  6. `worker_daemon.py::_json_safe` 对 ndarray 无预算先检查 `tolist()`：1e8 元素数组先物化巨型 Python list 再由 1MiB wire 检查（:838）拒绝 → 内存尖峰。
  7. `tests/test_worker_daemon_proxy.py` FakeSocket 无 `settimeout` → `_get_state` 调用 `self._s.settimeout(...)` 抛 AttributeError，3 errors（研究阶段实测 exit 1）。夹具失配，非 3 个产品 bug。
- 研究阶段证据继承（e6791c81 实测，blob 与 120677e6 相同的生产文件）：Pi 包装 10 PASS、Python runtime 包装 19 PASS、real socketpair 迟到误投/非法 UTF-8 探针、40-byte POSIX shm 探针；正式 daemon listener 在本环境 EPERM（ENVIRONMENT_BLOCKED）。120677e6 上 dev Python suite 76/76 PASS、canonical i18n gate PASS 为研究记录，不在本轨重复声明。
- 未执行腿（如实记录）：完整 CMake/CTest 全量、可见 Qt 桌面、真实 ORT/GPU、Windows、真实云模型。
