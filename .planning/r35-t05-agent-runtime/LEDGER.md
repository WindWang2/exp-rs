# LEDGER — r35-t05-agent-runtime（goal-loop 持久账本）

格式：`轮次 | 改动 | 验证 | 结果 | 下一步`

- R0 | worktree `zcode/r35-t05-agent-runtime-20261118T183744Z` @ `120677e6` 建立；fetch 核实 origin/master=研究快照 SHA；发现 3 个新 open PR（#2149/#2150/#2151）并建保留区 | git/gh API 实测 | 通过 | 源码事实逐条核实
- R1 | BASELINE/PATH_OWNERSHIP/DECISIONS/PLAN 落盘；7 条源码事实在 `120677e6` 上 SOURCE_CONFIRMED（固定 id 8001/8003、errors=replace ×3、_mount_shm_array 无 header 校验、create() 无溢出检查、onReadyRead 全缓冲判 bound、_json_safe 无预算、FakeSocket 缺 settimeout） | Read 逐行核对 | 通过 | 开始 WP11+WP13+WP14+WP15 生产代码
- R2 | 五项生产修复全部实现：worker_daemon.py（单调 proxy id ×2、strict UTF-8 ×3、_mount_shm_array header 权威校验+fd 无泄漏、_json_safe 预算 32B/元素）、shared_memory_segment.cpp create() 镜像 checked multiply、llm_streaming_client.cpp 尾部分离+ingestTransportBytes seam；测试：旧 proxy 套件修复（3/3 PASS）、新 identity 套件 13/13 PASS、C++ bounds 测试+SSE 分片 TEST_CASE 已写 | `python3 tests/test_r35_t05_worker_daemon_identity.py` exit0（13 tests OK）；`python3 tests/test_worker_daemon_proxy.py` exit0（3 tests OK）；阴性对照：同一套件对 HEAD 未修复 daemon 10/13 FAIL（迟到误投×2、UTF-8 改写、shm 校验×5、tolist 预算），3 个通过项恰为既有正确行为 | 通过 | CMake configure + 构建目标 + CTest
- R2a | configure 第一次失败（OpenCV 缺失，`| tail` 掩盖退出码——修正为重定向文件再取码）；按 r35-t03 同路径 `-DOpenCV_DIR=.../ui-build-deps/opencv/usr/lib/cmake/opencv5` 重配 | configure exit 0；构建 `test_r35_t05_agent_runtime_bounds test_llm_streaming_client` 双锁 -j4 exit 0（2300+ TU，qgis_core/gui 链） | 通过 | CTest 执行
- R3 | CTest：`-R "r35_t05|LlmStreamingClient|LlmConfigManager|corpus SSE|chunking|oversized|usage..."` → **30/30 passed, exit 0**（含新 C++ ×2、identity ctest 条目、全部既有 SSE/usage 回归）；第二遍 `-R r35_t05` 3/3；关键 Python 回归在 /tmp 新目录复验 exit 0 | ctest 实测 | 通过 | 4 个主题 commits + R1
- R4 | R1 独立 review（general-purpose subagent，读全量 diff+调用方+亲自跑测试）：**REQUEST_CHANGES** —— P1：id 序列 8000 起穿过 fire-and-forget ACK 域（host 对每个请求 echo id 回 ACK，reviewer 实证复现误投，无需超时；系本改动引入）；P2：32B/元素统一预算误拒 200×200 uint8 掩膜（原可交付）；P3×3（预算非字符串数组上界—接受；error 响应静默吞—既有行为记 HANDOFF；planning 未提交—收尾时提交） | REVIEW_R1.md 全文 | 完成 | 修 P1+P2
- R5 | R1 修复 commit `d0d1353435`：序列改 10000 起+注释更正+ACK 碰撞回归；预算改 dtype 感知（np.iinfo 位数+2 / 浮点 26+2 / 未知 32）+uint8 掩膜回归；identity 15/15、proxy 3/3、ctest 30/30 全绿 | 实测 | 通过 | R2 复查
- R6 | R2 复查（同 R1 独立 reviewer）：**APPROVE** —— P1 修复经其对抗复现+三 ACK 风暴实测不再复现；P2 dtype 估计实测各 dtype 上界成立、uint8 掩膜恢复交付（120,400B JSON）、float64 重数组仍前置拒绝；全量终扫无 sed 误伤/无残留常量。残留 3×P3 | REVIEW_R2.md | 完成 | P3 polish
- R7 | R2 P3 polish commit `5d787894f3`：有符号负极值预算双侧 bound、两处旧注释更正、fresh-import 生产常量守卫（identity 16/16）；最终 HEAD 全量定向 ctest **30/30 passed** | 实测 | 通过 | 规划文档提交 + push + PR
