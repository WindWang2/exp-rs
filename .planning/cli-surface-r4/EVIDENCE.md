# EVIDENCE — Track 14 验证证据链

## 1. 构建与资源纪律

- 构建目录：`build-gcc15`（全新，Ninja，Debug，`ENABLE_TESTS=ON`，`-DCMAKE_CXX_COMPILER=/usr/sbin/g++-15`）。
- 编译器选择理由：本机默认 g++ 16.2.1 对 Debug `-g` 多 TU ICE（既有记录）；gcc-15 下 `cmake/raise-compiler-stack.sh` 为 no-op，且配置显式置空 `CMAKE_CXX_COMPILER_LAUNCHER` 双保险（假绿陷阱防御，DECISIONS D8）。
- 资源红线：全程 `ninja -j2`；RSS 峰值远低于 70%（基线构建期间 available ≥ 40GB，无 `-j1` 降级事件）；`CTEST_PARALLEL_LEVEL=1`。
- 真重建纪律：改动 TU 先经 `-fsyntax-only`（真实编译旗标取自 `build-gcc15/compile_commands.json`）再进链接；关键验证双跑。

## 2. TDD 时序证据（test-first）

- `ef2ca1d117`（RED 锚点测试）先于 `5bd878d7a5`/`406dd2659e`（实现）入账 —— git 历史可证测试先于实现。
- RED 依据（代码考古，`git show ef2ca1d117^:src/cli/cli_dataset_commands.cpp` 第 160 行附近）：`fail()` 硬编码退出码 1；`pipeline` 缺文件落 ExecutionFailure(3)；`plugin uninstall` 未知 id 硬编码 1；`algorithms/models` 未知子命令静默滑入 list 退 0。新测试断言合同值 6/5/2 → 引入时必红。
- 单构建目录约束下的诚实声明：运行时 RED 截屏不可得（全库 `-j2` 构建约 11 小时，第二次 pristine 构建不经济）；以 git 时序 + 修前代码考古为 RED 证据。

## 3. 基线红绿分布

（构建完成后回填：`ctest -R "cli|exit_code|help" -j1` 首轮输出；区分 master 既有红与本轨道引入红。已知 master 既有红：`test_cli_command_surface` 的 docs 对账用例（README 缺 `session` 行）。）

## 4. 双跑日志

（回填：两轮 `ctest -R "cli|exit_code|help" -j1` 全量输出摘要 + 第二轮排除假阳性声明。）

## 5. 崩溃路径注入（WP-F）

- dispatch 层兜底边界：`5bd878d7a5`（dispatchCliCommand try/catch 三级：RSOperatorError 类型化→3/4、std::exception→3、...→3，全部经 finish 四元组出口）。
- 既有 catch 点审计（18 处）：run 的 RSOperatorError/exception 双 catch（3/4 类正确）；cli_batch_runner 两级 catch→批内隔离（非吞）；lab_batch_runner catch(...)?→per-student error verdict（隔离语义，合规）；sicnu_worker_main catch→结构化错误帧（合规）；其余为 GeoError 类型化 catch→finish。零静默返回 0。
- 注入用例：真实故障注入（坏输入/不可达路径）在 `test_surface_parity_r4`（缺参拒于 store 创建前、未知资源 5、schema 坏 2）与 `test_cli_fail_fast_r4`（副作用零证据 = store 文件不存在）覆盖；进程级 SIGINT 竞态注入按 CONTRACT 行 N/A 论证不做 flaky 断言。

## 6. 未解决项（移交对象明确）

1. MCP 侧 data-platform 工具错误无结构化 code（`data_platform_tools.cpp:139` fail() 抛裸 runtime_error）——src/agent 白名单外，移交 agent-MCP 面 track（对照 SURFACE_PARITY #3/#8）。
2. MCP resume_workflow 的 run_id 字符集 parse 期校验，CLI 侧 workflow resume 无等价校验（走 runner 错误→3，非静默）——补齐属 CLI 侧可做但涉及 runner 契约面，留待下一轮。
3. `data` 命令位置 URL 遗留语法（未知子命令≠错误）——本轨道保持（SURFACE_PARITY 行 8 记录），如需收紧属 breaking 决策。
4. GUI 帮助面板（CommandRegistry 81 词表）轴归 #1339；本轨道 CLI 三级面已收口。
5. `env-doctor` 2 类退出与环境强相关，ctest 不做机器相关断言（CONTRACT 行 14 论证）。
