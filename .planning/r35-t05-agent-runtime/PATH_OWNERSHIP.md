# PATH_OWNERSHIP — r35-t05-agent-runtime

## 本轨写集（本 PR 实际修改）

| 路径 | 类别 |
|---|---|
| `src/python/scripts/worker_daemon.py` | T05 主写集（代理 id、严格 UTF-8、shm mount 校验、json 预算） |
| `src/python/isolated/shared_memory_segment.cpp` | T05 主写集（create() checked multiply/add） |
| `src/agent/llm_streaming_client.cpp` | T05 主写集（onReadyRead 行边界与尾部分离） |
| `tests/test_worker_daemon_proxy.py` | T05 Agent/Python 既有测试（FakeSocket 夹具修复，恢复 3 条回归） |
| `tests/test_llm_streaming_client.cpp` | T05 Agent 既有测试（追加分片无关 TEST_CASE，不动既有块） |
| `tests/test_r35_t05_worker_daemon_identity.py` | 新建（本轨前缀） |
| `tests/test_r35_t05_agent_runtime_bounds.cpp` | 新建（本轨前缀） |
| `tests/CMakeLists.txt` | 共同注册处：仅追加本轨唯一块 |
| `.gitignore` | 仅追加本轨 planning 白名单三行 |

## 启动时 open PR 保留区（2026-11-19 核实）

- #2149（T06）：`.gitignore`、`.planning/r35-t06-*`、`resources/translations/sicnu_zh_CN.ts`
- #2150（T01）：`src/processing/algorithms/{change_detection,image_enhancement,satellite_products,spectral_unmixing}.*`、`tests/test_r35_t01_*`
- #2151（T03）：`src/dataset/dataset_manifest.cpp`、`src/geospatial/**`（catalog/fabric/metadata/raster）、`tests/test_r35_t03_*`

→ 与本轨写集零整文件碰撞。`.gitignore`/`tests/CMakeLists.txt` 为 append-only 共享点。

## 明确不动（他人 owner / 已合并基线）

- `src/agent/runtime/**` 本轮不触碰（WP03/WP04 累计预算与 dock 批量渲染为后续包，避免单 PR 过大）。
- Mission/AgentOps、`src/agent_loop/session_journal.*`：#2143 已合并基线，本轮无真实残差修复需求，不重复实现。
- `python_worker_provider.*`（T02）、`src/data/**`（T03）、TaskCenter/OutputCommitter（T04）、全局 TS 与 main_window_*（T06）。
- `src/verification/**` 归档不接入。
